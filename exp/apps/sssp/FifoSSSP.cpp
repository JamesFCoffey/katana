/** Single source shortest paths -*- C++ -*-
 * @example SSSP.cpp
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @section Description
 *
 * Single source shortest paths.
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 * @author Yi-Shan Lu <yishanlu@cs.utexas.edu>
 */
#include "Galois/Galois.h"
#include "Galois/Accumulator.h"
#include "Galois/Bag.h"
#include "Galois/Statistic.h"
#include "Galois/Timer.h"
#include "Galois/Graphs/LCGraph.h"
#include "Galois/Graphs/TypeTraits.h"
#include "llvm/Support/CommandLine.h"
#include "Lonestar/BoilerPlate.h"
#include "Galois/WorkList/WorkSet.h"
#include "Galois/WorkList/MarkingSet.h"

#include <iostream>
#include <deque>
#include <set>

#include "SSSP.h"
#include "GraphLabAlgo.h"
#include "LigraAlgo.h"

namespace cll = llvm::cl;

static const char* name = "Single Source Shortest Path";
static const char* desc =
  "Computes the shortest path from a source node to all nodes in a directed "
  "graph using a modified chaotic iteration algorithm";
static const char* url = "single_source_shortest_path";

enum Algo {
  async,
  asyncFifo,
  asyncBlindObim,
  asyncBlindFifo,
  asyncBlindFifoHSet,
  asyncBlindFifoMSet,
  asyncBlindFifoOSet,
  asyncBlindObimHSet,
  asyncBlindObimMSet,
  asyncBlindObimOSet,
  asyncWithCas,
  asyncWithCasFifo,
  asyncWithCasBlindObim,
  asyncWithCasBlindFifo,
  asyncWithCasBlindFifoHSet,
  asyncWithCasBlindFifoMSet,
  asyncWithCasBlindFifoOSet,
  asyncWithCasBlindObimHSet,
  asyncWithCasBlindObimMSet,
  asyncWithCasBlindObimOSet,
  asyncPP,
  graphlab,
  ligra,
  ligraChi,
  serial
};

static cll::opt<std::string> filename(cll::Positional, cll::desc("<input graph>"), cll::Required);
static cll::opt<std::string> transposeGraphName("graphTranspose", cll::desc("Transpose of input graph"));
static cll::opt<bool> symmetricGraph("symmetricGraph", cll::desc("Input graph is symmetric"));
static cll::opt<unsigned int> startNode("startNode", cll::desc("Node to start search from"), cll::init(0));
static cll::opt<unsigned int> reportNode("reportNode", cll::desc("Node to report distance to"), cll::init(1));
static cll::opt<int> stepShift("delta", cll::desc("Shift value for the deltastep"), cll::init(10));
cll::opt<unsigned int> memoryLimit("memoryLimit",
    cll::desc("Memory limit for out-of-core algorithms (in MB)"), cll::init(~0U));
static cll::opt<Algo> algo("algo", cll::desc("Choose an algorithm:"),
    cll::values(
      clEnumValN(Algo::async, "async", "Asynchronous"),
      clEnumValN(Algo::asyncFifo, "asyncFifo", "async with dChunkedFIFO scheduler"),
      clEnumValN(Algo::asyncBlindObim, "asyncBlindObim", "async without discarding empty work"),
      clEnumValN(Algo::asyncBlindFifo, "asyncBlindFifo", "asyncBlind with dChunkedFIFO scheduling"),
      clEnumValN(Algo::asyncBlindFifoHSet, "asyncBlindFifoHSet", "asyncBlindFifo with two-level hash uni-set scheduler"),
      clEnumValN(Algo::asyncBlindFifoMSet, "asyncBlindFifoMSet", "asyncBlindFifo with marking set uni-set scheduler"),
      clEnumValN(Algo::asyncBlindFifoOSet, "asyncBlindFifoOSet", "asyncBlindFifo with two-level set uni-set scheduler"),
      clEnumValN(Algo::asyncBlindObimHSet, "asyncBlindObimHSet", "asyncBlindObim with two-level hash uni-set scheduler"),
      clEnumValN(Algo::asyncBlindObimMSet, "asyncBlindObimMSet", "asyncBlindObim with marking set uni-set scheduler"),
      clEnumValN(Algo::asyncBlindObimOSet, "asyncBlindObimOSet", "asyncBlindObim with two-level set uni-set scheduler"),
      clEnumValN(Algo::asyncPP, "asyncPP", "Async, CAS, push-pull"),
      clEnumValN(Algo::asyncWithCas, "asyncWithCas", "Use compare-and-swap to update nodes"),
      clEnumValN(Algo::asyncWithCasFifo, "asyncWithCasFifo", "asyncWithCas with dChunkedFIFO scheduler"),
      clEnumValN(Algo::asyncWithCasBlindObim, "asyncWithCasBlindObim", "asyncWithCas without discarding empty work"),
      clEnumValN(Algo::asyncWithCasBlindFifo, "asyncWithCasBlindFifo", "asyncWithCasBlind with dChunkedFIFO scheduling"),
      clEnumValN(Algo::asyncWithCasBlindFifoHSet, "asyncWithCasBlindFifoHSet", "asyncWithCasBlindFifo with two-level hash uni-set scheduler"),
      clEnumValN(Algo::asyncWithCasBlindFifoMSet, "asyncWithCasBlindFifoMSet", "asyncWithCasBlindFifo with marking set uni-set scheduler"),
      clEnumValN(Algo::asyncWithCasBlindFifoOSet, "asyncWithCasBlindFifoOSet", "asyncWithCasBlindFifo with two-level set uni-set scheduler"),
      clEnumValN(Algo::asyncWithCasBlindObimHSet, "asyncWithCasBlindObimHSet", "asyncWithCasBlindObim with two-level hash uni-set scheduler"),
      clEnumValN(Algo::asyncWithCasBlindObimMSet, "asyncWithCasBlindObimMSet", "asyncWithCasBlindObim with marking set uni-set scheduler"),
      clEnumValN(Algo::asyncWithCasBlindObimOSet, "asyncWithCasBlindObimOSet", "asyncWithCasBlindObim with two-level set uni-set scheduler"),
      clEnumValN(Algo::serial, "serial", "Serial"),
      clEnumValN(Algo::graphlab, "graphlab", "Use GraphLab programming model"),
      clEnumValN(Algo::ligraChi, "ligraChi", "Use Ligra and GraphChi programming model"),
      clEnumValN(Algo::ligra, "ligra", "Use Ligra programming model"),
      clEnumValEnd), cll::init(Algo::asyncWithCas));

static const bool trackWork = true;
static Galois::Statistic* BadWork;
static Galois::Statistic* WLEmptyWork;

template<typename Graph>
struct not_visited {
  Graph& g;

  not_visited(Graph& g): g(g) { }

  bool operator()(typename Graph::GraphNode n) const {
    return g.getData(n).dist >= DIST_INFINITY;
  }
};

template<typename Graph, typename Enable = void>
struct not_consistent {
  not_consistent(Graph& g) { }

  bool operator()(typename Graph::GraphNode n) const { return false; }
};

template<typename Graph> 
struct not_consistent<Graph, typename std::enable_if<!Galois::Graph::is_segmented<Graph>::value>::type> {
  Graph& g;
  not_consistent(Graph& g): g(g) { }

  bool operator()(typename Graph::GraphNode n) const {
    Dist dist = g.getData(n).dist;
    if (dist == DIST_INFINITY)
      return false;

    for (typename Graph::edge_iterator ii = g.edge_begin(n), ee = g.edge_end(n); ii != ee; ++ii) {
      Dist ddist = g.getData(g.getEdgeDst(ii)).dist;
      Dist w = g.getEdgeData(ii);
      if (ddist > dist + w) {
        //std::cout << ddist << " " << dist + w << " " << n << " " << g.getEdgeDst(ii) << "\n"; // XXX
	return true;
      }
    }
    return false;
  }
};

template<typename Graph>
struct max_dist {
  Graph& g;
  Galois::GReduceMax<Dist>& m;

  max_dist(Graph& g, Galois::GReduceMax<Dist>& m): g(g), m(m) { }

  void operator()(typename Graph::GraphNode n) const {
    Dist d = g.getData(n).dist;
    if (d == DIST_INFINITY)
      return;
    m.update(d);
  }
};

template<typename UpdateRequest>
struct UpdateRequestIndexer: public std::unary_function<UpdateRequest, unsigned int> {
  unsigned int operator() (const UpdateRequest& val) const {
    unsigned int t = val.w >> stepShift;
    return t;
  }
};

template<typename Graph>
bool verify(Graph& graph, typename Graph::GraphNode source) {
  if (graph.getData(source).dist != 0) {
    std::cerr << "source has non-zero dist value\n";
    return false;
  }
  namespace pstl = Galois::ParallelSTL;

  size_t notVisited = pstl::count_if(graph.begin(), graph.end(), not_visited<Graph>(graph));
  if (notVisited) {
    std::cerr << notVisited << " unvisited nodes; this is an error if the graph is strongly connected\n";
  }

  bool consistent = pstl::find_if(graph.begin(), graph.end(), not_consistent<Graph>(graph)) == graph.end();
  if (!consistent) {
    std::cerr << "node found with incorrect distance\n";
    return false;
  }

  Galois::GReduceMax<Dist> m;
  Galois::do_all(graph.begin(), graph.end(), max_dist<Graph>(graph, m));
  std::cout << "max dist: " << m.reduce() << "\n";
  
  return true;
}

template<typename Algo>
void initialize(Algo& algo,
    typename Algo::Graph& graph,
    typename Algo::Graph::GraphNode& source,
    typename Algo::Graph::GraphNode& report) {

  algo.readGraph(graph);
  std::cout << "Read " << graph.size() << " nodes\n";

  if (startNode >= graph.size() || reportNode >= graph.size()) {
    std::cerr 
      << "failed to set report: " << reportNode 
      << " or failed to set source: " << startNode << "\n";
    assert(0);
    abort();
  }
  
  typename Algo::Graph::iterator it = graph.begin();
  std::advance(it, startNode);
  source = *it;
  it = graph.begin();
  std::advance(it, reportNode);
  report = *it;
}

template<typename Graph>
void readInOutGraph(Graph& graph) {
  using namespace Galois::Graph;
  if (symmetricGraph) {
    //! [Reading a graph]
    Galois::Graph::readGraph(graph, filename);
    //! [Reading a graph]
  } else if (transposeGraphName.size()) {
    Galois::Graph::readGraph(graph, filename, transposeGraphName);
  } else {
    GALOIS_DIE("Graph type not supported");
  }
}

struct SerialAlgo {
  //! [Define LC_CSR_Graph]  
  typedef Galois::Graph::LC_CSR_Graph<SNode, uint32_t>
    ::with_no_lockable<true>::type Graph;
  //! [Define LC_CSR_Graph]  

  typedef Graph::GraphNode GNode;
  typedef UpdateRequestCommon<GNode> UpdateRequest;

  std::string name() const { return "Serial"; }
  void readGraph(Graph& graph) { Galois::Graph::readGraph(graph, filename); }

  struct Initialize {
    Graph& g;
    Initialize(Graph& g): g(g) { }

    void operator()(Graph::GraphNode n) const {
      g.getData(n).dist = DIST_INFINITY;
    }
  };

  void operator()(Graph& graph, const GNode src) const {
    std::set<UpdateRequest, std::less<UpdateRequest> > initial;
    UpdateRequest init(src, 0);
    initial.insert(init);

    Galois::Statistic counter("Iterations");
    
    while (!initial.empty()) {
      counter += 1;
      UpdateRequest req = *initial.begin();
      initial.erase(initial.begin());
      SNode& data = graph.getData(req.n, Galois::MethodFlag::UNPROTECTED);
      if (req.w < data.dist) {
        data.dist = req.w;
	for (Graph::edge_iterator
	      ii = graph.edge_begin(req.n, Galois::MethodFlag::UNPROTECTED), 
	      ee = graph.edge_end(req.n, Galois::MethodFlag::UNPROTECTED);
	    ii != ee; ++ii) {
          GNode dst = graph.getEdgeDst(ii);
          Dist d = graph.getEdgeData(ii);
          Dist newDist = req.w + d;
          if (newDist < graph.getData(dst, Galois::MethodFlag::UNPROTECTED).dist) {
            initial.insert(UpdateRequest(dst, newDist));
	  }
        }
      }
    }
  }
};

template<bool UseCas>
struct AsyncAlgo {
  typedef SNode Node;
  
  // ! [Define LC_InlineEdge_Graph]
  typedef Galois::Graph::LC_InlineEdge_Graph<Node, uint32_t>
    ::template with_out_of_line_lockable<true>::type
    ::template with_compressed_node_ptr<true>::type
    ::template with_numa_alloc<true>::type
    Graph;
  // ! [Define LC_InlineEdge_Graph]
  
  typedef typename Graph::GraphNode GNode;
  typedef UpdateRequestCommon<GNode> UpdateRequest;

  std::string name() const {
    return UseCas ? "Asynchronous with CAS" : "Asynchronous"; 
  }

  void readGraph(Graph& graph) { Galois::Graph::readGraph(graph, filename); }

  struct Initialize {
    Graph& g;
    Initialize(Graph& g): g(g) { }
    void operator()(typename Graph::GraphNode n) const {
      g.getData(n, Galois::MethodFlag::UNPROTECTED).dist = DIST_INFINITY;
    }
  };

  template<typename Pusher>
  void relaxEdge(Graph& graph, Dist sdist, typename Graph::edge_iterator ii, Pusher& pusher) {
    GNode dst = graph.getEdgeDst(ii);
    Dist d = graph.getEdgeData(ii);
    auto& ddata = graph.getData(dst, Galois::MethodFlag::UNPROTECTED).dist;
    Dist newDist = sdist + d;
    Dist oldDist;
    while (newDist < (oldDist = ddata)) {
      if (!UseCas || ddata.compare_exchange_weak(oldDist, newDist, std::memory_order_acq_rel)) { // __sync_bool_compare_and_swap(&ddata.dist, oldDist, newDist)) {
        if (!UseCas)
          ddata = newDist;
        if (trackWork && oldDist != DIST_INFINITY)
          *BadWork += 1;
        pusher.push(UpdateRequest(dst, newDist));
        break;
      }
    }
  }

  template<typename Pusher>
  void relaxNode(Graph& graph, UpdateRequest& req, Pusher& pusher) {
    const Galois::MethodFlag flag = UseCas ? Galois::MethodFlag::UNPROTECTED : Galois::MethodFlag::WRITE;
    auto& sdist = graph.getData(req.n, flag).dist;

    if (req.w != sdist) {
      if (trackWork)
        *WLEmptyWork += 1;
      return;
    }

    for (typename Graph::edge_iterator ii = graph.edge_begin(req.n, flag), ei = graph.edge_end(req.n, flag); ii != ei; ++ii) {
      if (req.w != sdist) {
        if (trackWork)
          *WLEmptyWork += 1;
        break;
      }
      relaxEdge(graph, sdist, ii, pusher);
    }
  }

  struct Process {
    AsyncAlgo* self;
    Graph& graph;
    Process(AsyncAlgo* s, Graph& g): self(s), graph(g) { }
    void operator()(UpdateRequest& req, Galois::UserContext<UpdateRequest>& ctx) {
      self->relaxNode(graph, req, ctx);
    }
  };

  typedef Galois::InsertBag<UpdateRequest> Bag;

  struct InitialProcess {
    AsyncAlgo* self;
    Graph& graph;
    Bag& bag;
    Node& sdata;
    InitialProcess(AsyncAlgo* s, Graph& g, Bag& b, Node& d): self(s), graph(g), bag(b), sdata(d) { }
    void operator()(typename Graph::edge_iterator ii) const {
      self->relaxEdge(graph, sdata.dist, ii, bag);
    }
  };

  void operator()(Graph& graph, GNode source) {
    using namespace Galois::WorkList;
    typedef ChunkedFIFO<64> Chunk;
    typedef OrderedByIntegerMetric<UpdateRequestIndexer<UpdateRequest>, Chunk, 10, false> OBIM;

    std::cout << "INFO: Using delta-step of " << (1 << stepShift) << "\n";
    std::cout << "WARNING: Performance varies considerably due to delta parameter.\n";
    std::cout << "WARNING: Do not expect the default to be good for your graph.\n";

    Bag initial;
    graph.getData(source).dist = 0;
    Galois::do_all(
        graph.out_edges(source, Galois::MethodFlag::UNPROTECTED).begin(),
        graph.out_edges(source, Galois::MethodFlag::UNPROTECTED).end(),
        InitialProcess(this, graph, initial, graph.getData(source)));
    if(algo == Algo::asyncFifo || algo == Algo::asyncWithCasFifo)
      Galois::for_each_local(initial, Process(this, graph), Galois::wl<dChunkedFIFO<64> >());
    else
      Galois::for_each_local(initial, Process(this, graph), Galois::wl<OBIM>());
  }
};

struct SetNode {
  Dist dist;
  bool inSet;
};

template<typename Graph>
struct NodeIndexer: public std::unary_function<typename Graph::GraphNode, unsigned int> {
  Graph& graph;
  NodeIndexer(Graph& g): graph(g) {}

  unsigned int operator() (const typename Graph::GraphNode n) const {
    return graph.getData(n, Galois::MethodFlag::UNPROTECTED).dist >> stepShift;
  }
};

template<typename Graph>
struct NodeSetMarker: public std::unary_function<typename Graph::GraphNode, bool*> {
  Graph& graph;
  NodeSetMarker(Graph& g): graph(g) {}

  bool* operator() (const typename Graph::GraphNode n) const {
    return &(graph.getData(n, Galois::MethodFlag::UNPROTECTED).inSet);
  }
};

template<bool UseCas>
struct AsyncSetAlgo {
  typedef SetNode Node;
  
  // ! [Define LC_InlineEdge_Graph]
  typedef Galois::Graph::LC_InlineEdge_Graph<Node, uint32_t>
    ::template with_out_of_line_lockable<true>::type
    ::template with_compressed_node_ptr<true>::type
    ::template with_numa_alloc<true>::type
    Graph;
  // ! [Define LC_InlineEdge_Graph]
  
  typedef typename Graph::GraphNode GNode;

  std::string name() const {
    return UseCas ? "Asynchronous Set with CAS" : "Asynchronous Set"; 
  }

  void readGraph(Graph& graph) { Galois::Graph::readGraph(graph, filename); }

  struct Initialize {
    Graph& g;
    Initialize(Graph& g): g(g) { }
    void operator()(typename Graph::GraphNode n) const {
      auto& data = g.getData(n, Galois::MethodFlag::UNPROTECTED);
      data.dist = DIST_INFINITY;
      data.inSet = false;
    }
  };

  template<typename Pusher>
  void relaxEdge(Graph& graph, Node& sdata, typename Graph::edge_iterator ii, Pusher& pusher) {
    GNode dst = graph.getEdgeDst(ii);
    Dist d = graph.getEdgeData(ii);
    Node& ddata = graph.getData(dst, Galois::MethodFlag::UNPROTECTED);
    Dist newDist = sdata.dist + d;
    Dist oldDist;
    while (newDist < (oldDist = ddata.dist)) {
      if (!UseCas || __sync_bool_compare_and_swap(&ddata.dist, oldDist, newDist)) {
        if (!UseCas)
          ddata.dist = newDist;
        if (trackWork && oldDist != DIST_INFINITY)
          *BadWork += 1;
        pusher.push(dst);
        break;
      }
    }
  }

  template<typename Pusher>
  void relaxNode(Graph& graph, GNode req, Pusher& pusher) {
    const Galois::MethodFlag flag = UseCas ? Galois::MethodFlag::UNPROTECTED : Galois::MethodFlag::WRITE;
    Node& sdata = graph.getData(req, flag);

    for (typename Graph::edge_iterator ii = graph.edge_begin(req, flag), ei = graph.edge_end(req, flag); ii != ei; ++ii) {
      relaxEdge(graph, sdata, ii, pusher);
    }
  }

  struct Process {
    AsyncSetAlgo* self;
    Graph& graph;
    Process(AsyncSetAlgo* s, Graph& g): self(s), graph(g) { }
    void operator()(GNode req, Galois::UserContext<GNode>& ctx) {
      self->relaxNode(graph, req, ctx);
    }
  };

  typedef Galois::InsertBag<GNode> Bag;

  struct InitialProcess {
    AsyncSetAlgo* self;
    Graph& graph;
    Bag& bag;
    Node& sdata;
    InitialProcess(AsyncSetAlgo* s, Graph& g, Bag& b, Node& d): self(s), graph(g), bag(b), sdata(d) { }
    void operator()(typename Graph::edge_iterator ii) const {
      self->relaxEdge(graph, sdata, ii, bag);
    }
  };

  void operator()(Graph& graph, GNode source) {
    using namespace Galois::WorkList;
    typedef ChunkedFIFO<64> Chunk;
    typedef OrderedByIntegerMetric<NodeIndexer<Graph>, Chunk, 10, false> OBIM;
    typedef dChunkedMarkingSetFIFO<NodeSetMarker<Graph>,64> MSet;
    typedef dChunkedTwoLevelSetFIFO<64> OSet;
    typedef dChunkedTwoLevelHashFIFO<64> HSet;
    typedef detail::MarkingWorkSetMaster<GNode,NodeSetMarker<Graph>,OBIM> ObimMSet;
    typedef detail::WorkSetMaster<GNode,OBIM,Galois::ThreadSafeTwoLevelSet<GNode> > ObimOSet;
    typedef detail::WorkSetMaster<GNode,OBIM,Galois::ThreadSafeTwoLevelHash<GNode> > ObimHSet;

    Bag initial;
    graph.getData(source).dist = 0;
    Galois::do_all(
        graph.out_edges(source, Galois::MethodFlag::UNPROTECTED).begin(),
        graph.out_edges(source, Galois::MethodFlag::UNPROTECTED).end(),
        InitialProcess(this, graph, initial, graph.getData(source)));

    auto marker = NodeSetMarker<Graph>(graph);
    auto indexer = NodeIndexer<Graph>(graph);

    switch(algo) {
    case Algo::asyncBlindFifoMSet:
    case Algo::asyncWithCasBlindFifoMSet:
      Galois::for_each_local(initial, Process(this, graph), Galois::wl<MSet>(marker));
      break;
    case Algo::asyncBlindFifoOSet:
    case Algo::asyncWithCasBlindFifoOSet:
      Galois::for_each_local(initial, Process(this, graph), Galois::wl<OSet>());
      break;
    case Algo::asyncBlindFifoHSet:
    case Algo::asyncWithCasBlindFifoHSet:
      Galois::for_each_local(initial, Process(this, graph), Galois::wl<HSet>());
      break;
    case Algo::asyncBlindFifo:
    case Algo::asyncWithCasBlindFifo:
      Galois::for_each_local(initial, Process(this, graph), Galois::wl<dChunkedFIFO<64> >());
      break;
    case Algo::asyncBlindObimMSet:
    case Algo::asyncWithCasBlindObimMSet:
      Galois::for_each_local(initial, Process(this, graph), Galois::wl<ObimMSet>(marker,dummy,indexer));
      break;
    case Algo::asyncBlindObimOSet:
    case Algo::asyncWithCasBlindObimOSet:
      Galois::for_each_local(initial, Process(this, graph), Galois::wl<ObimOSet>(dummy,indexer));
      break;
    case Algo::asyncBlindObimHSet:
    case Algo::asyncWithCasBlindObimHSet:
      Galois::for_each_local(initial, Process(this, graph), Galois::wl<ObimHSet>(dummy,indexer));
      break;
    case Algo::asyncBlindObim:
    case Algo::asyncWithCasBlindObim:
    default:
      std::cout << "INFO: Using delta-step of " << (1 << stepShift) << "\n";
      std::cout << "WARNING: Performance varies considerably due to delta parameter.\n";
      std::cout << "WARNING: Do not expect the default to be good for your graph.\n";
      Galois::for_each_local(initial, Process(this, graph), Galois::wl<OBIM>(NodeIndexer<Graph>(graph)));
      break;
    } // end switch
  }
};

struct AsyncAlgoPP {
  typedef SNode Node;

  typedef Galois::Graph::LC_InlineEdge_Graph<Node, uint32_t>
    ::with_out_of_line_lockable<true>::type
    ::with_compressed_node_ptr<true>::type
    ::with_numa_alloc<true>::type
    Graph;
  typedef Graph::GraphNode GNode;
  typedef UpdateRequestCommon<GNode> UpdateRequest;

  std::string name() const {
    return "Asynchronous with CAS and Push and pull";
  }

  void readGraph(Graph& graph) { Galois::Graph::readGraph(graph, filename); }

  struct Initialize {
    Graph& g;
    Initialize(Graph& g): g(g) { }
    void operator()(Graph::GraphNode n) const {
      g.getData(n, Galois::MethodFlag::UNPROTECTED).dist = DIST_INFINITY;
    }
  };

  template <typename Pusher>
  void relaxEdge(Graph& graph, Dist& sdata, typename Graph::edge_iterator ii, Pusher& pusher) {
      GNode dst = graph.getEdgeDst(ii);
      Dist d = graph.getEdgeData(ii);
      Node& ddata = graph.getData(dst, Galois::MethodFlag::UNPROTECTED);
      Dist newDist = sdata + d;
      Dist oldDist;
      if (newDist < (oldDist = ddata.dist)) {
        do {
          if (ddata.dist.compare_exchange_weak(oldDist, newDist, std::memory_order_acq_rel)) { //__sync_bool_compare_and_swap(&ddata.dist, oldDist, newDist)) {
            if (trackWork && oldDist != DIST_INFINITY)
              *BadWork += 1;
            pusher.push(UpdateRequest(dst, newDist));
            break;
          }
        } while (newDist < (oldDist = ddata.dist));
      } else {
        sdata = std::min(oldDist + d, sdata);
      }
    }

  struct Process {
    AsyncAlgoPP* self;
    Graph& graph;
    Process(AsyncAlgoPP* s, Graph& g): self(s), graph(g) { }

    void operator()(UpdateRequest& req, Galois::UserContext<UpdateRequest>& ctx) {
      const Galois::MethodFlag flag = Galois::MethodFlag::UNPROTECTED;
      Node& sdata = graph.getData(req.n, flag);
      auto& psdist = sdata.dist;
      Dist sdist = psdist;

      if (req.w != sdist) {
        if (trackWork)
          *WLEmptyWork += 1;
        return;
      }

      for (Graph::edge_iterator ii = graph.edge_begin(req.n, flag), ei = graph.edge_end(req.n, flag); ii != ei; ++ii) {
        self->relaxEdge(graph, sdist, ii, ctx);
      }

      // //try doing a pull
      // Dist oldDist;
      // while (sdist < (oldDist = *psdist)) {
      //   if (__sync_bool_compare_and_swap(psdist, oldDist, sdist)) {
      //     req.w = sdist;
      //     operator()(req, ctx);
      //   }
      // }
    }
  };

  typedef Galois::InsertBag<UpdateRequest> Bag;

  struct InitialProcess {
    AsyncAlgoPP* self;
    Graph& graph;
    Bag& bag;
    InitialProcess(AsyncAlgoPP* s, Graph& g, Bag& b): self(s), graph(g), bag(b) { }
    void operator()(Graph::edge_iterator ii) const {
      Dist d = 0;
      self->relaxEdge(graph, d, ii, bag);
    }
  };

  void operator()(Graph& graph, GNode source) {
    using namespace Galois::WorkList;
    typedef ChunkedFIFO<64> Chunk;
    typedef OrderedByIntegerMetric<UpdateRequestIndexer<UpdateRequest>, Chunk, 10, false> OBIM;

    std::cout << "INFO: Using delta-step of " << (1 << stepShift) << "\n";
    std::cout << "WARNING: Performance varies considerably due to delta parameter.\n";
    std::cout << "WARNING: Do not expect the default to be good for your graph.\n";

    Bag initial;
    graph.getData(source).dist = 0;
    Galois::do_all(
        graph.out_edges(source, Galois::MethodFlag::UNPROTECTED).begin(),
        graph.out_edges(source, Galois::MethodFlag::UNPROTECTED).end(),
        InitialProcess(this, graph, initial));
    Galois::for_each_local(initial, Process(this, graph), Galois::wl<OBIM>());
  }
};

namespace Galois { namespace DEPRECATED {
template<>
struct does_not_need_aborts<AsyncAlgo<true>::Process> : public boost::true_type {};
}
}

static_assert(Galois::DEPRECATED::does_not_need_aborts<AsyncAlgo<true>::Process>::value, "Oops");

template<typename Algo>
void run(bool prealloc = true) {
  typedef typename Algo::Graph Graph;
  typedef typename Graph::GraphNode GNode;

  Algo algo;
  Graph graph;
  GNode source, report;

  initialize(algo, graph, source, report);

  size_t approxNodeData = graph.size() * 64;
  //size_t approxEdgeData = graph.sizeEdges() * sizeof(typename Graph::edge_data_type) * 2;
  if (prealloc)
    Galois::preAlloc(numThreads + approxNodeData / Galois::Runtime::pagePoolSize());
  Galois::reportPageAlloc("MeminfoPre");

  Galois::StatTimer T;
  std::cout << "Running " << algo.name() << " version\n";
  T.start();
  Galois::do_all_local(graph, typename Algo::Initialize(graph));
  algo(graph, source);
  T.stop();
  
  Galois::reportPageAlloc("MeminfoPost");
  Galois::Runtime::reportNumaAlloc("NumaPost");

  std::cout << "Node " << reportNode << " has distance " << graph.getData(report).dist << "\n";

  if (!skipVerify) {
    if (verify(graph, source)) {
      std::cout << "Verification successful.\n";
    } else {
      GALOIS_DIE("Verification failed");
    }
  }
}

int main(int argc, char **argv) {
  Galois::StatManager statManager;
  LonestarStart(argc, argv, name, desc, url);

  if (trackWork) {
    BadWork = new Galois::Statistic("BadWork");
    WLEmptyWork = new Galois::Statistic("EmptyWork");
  }

  Galois::StatTimer T("TotalTime");
  T.start();
  switch (algo) {
    case Algo::serial: run<SerialAlgo>(); break;
    case Algo::async: run<AsyncAlgo<false> >(); break;
    case Algo::asyncFifo: run<AsyncAlgo<false> >(); break;
    case Algo::asyncBlindObim: run<AsyncSetAlgo<false> >(); break;
    case Algo::asyncBlindFifo: run<AsyncSetAlgo<false> >(); break;
    case Algo::asyncBlindFifoHSet: run<AsyncSetAlgo<false> >(); break;
    case Algo::asyncBlindFifoMSet: run<AsyncSetAlgo<false> >(); break;
    case Algo::asyncBlindFifoOSet: run<AsyncSetAlgo<false> >(); break;
    case Algo::asyncBlindObimHSet: run<AsyncSetAlgo<false> >(); break;
    case Algo::asyncBlindObimMSet: run<AsyncSetAlgo<false> >(); break;
    case Algo::asyncBlindObimOSet: run<AsyncSetAlgo<false> >(); break;
    case Algo::asyncWithCas: run<AsyncAlgo<true> >(); break;
    case Algo::asyncWithCasFifo: run<AsyncAlgo<true> >(); break;
    case Algo::asyncWithCasBlindObim: run<AsyncSetAlgo<true> >(); break;
    case Algo::asyncWithCasBlindFifo: run<AsyncSetAlgo<true> >(); break;
    case Algo::asyncWithCasBlindFifoHSet: run<AsyncSetAlgo<true> >(); break;
    case Algo::asyncWithCasBlindFifoMSet: run<AsyncSetAlgo<true> >(); break;
    case Algo::asyncWithCasBlindFifoOSet: run<AsyncSetAlgo<true> >(); break;
    case Algo::asyncWithCasBlindObimHSet: run<AsyncSetAlgo<true> >(); break;
    case Algo::asyncWithCasBlindObimMSet: run<AsyncSetAlgo<true> >(); break;
    case Algo::asyncWithCasBlindObimOSet: run<AsyncSetAlgo<true> >(); break;
    case Algo::asyncPP: run<AsyncAlgoPP>(); break;
      //    case Algo::ligra: run<LigraAlgo<false> >(); break;
      //    case Algo::ligraChi: run<LigraAlgo<true> >(false); break;
    case Algo::graphlab: run<GraphLabAlgo>(); break;
    default: std::cerr << "Unknown algorithm\n"; abort();
  }
  T.stop();

  if (trackWork) {
    delete BadWork;
    delete WLEmptyWork;
  }

  return 0;
}
