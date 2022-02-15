import os
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any, Collection, Union

import numpy as np
import pandas
import pytest

from katana.example_data import get_misc_dataset
from katana.local import EntityTypeArray, Graph
from katana.local.import_data import (
    from_adjacency_matrix,
    from_csr,
    from_edge_list_arrays,
    from_edge_list_dataframe,
    from_edge_list_matrix,
    from_graphml,
    from_sorted_edge_list_arrays,
)


def range_equivalent(a: Union[range, Any], b: Union[range, Any]):
    """
    :return: True if two range like objects have the same start, stop, and step.
        They need not be the same type.
    """
    return a.start == b.start and a.stop == b.stop and a.step == b.step


def ranges_all_equivalent(a: Collection, b: Collection):
    """
    :return: True iff two sequences of ranges are equivalent.
    :see: `range_equivalent`
    """
    return all(range_equivalent(x, y) for x, y in zip(a, b))


def test_adjacency_matrix():
    g = from_adjacency_matrix(np.array([[0, 1, 0], [0, 0, 2], [3, 0, 0]]))
    assert ranges_all_equivalent([g.out_edge_ids(n) for n in g.nodes()], [range(0, 1), range(1, 2), range(2, 3)])
    assert [g.get_edge_dst(i) for i in range(g.num_edges())] == [1, 2, 0]
    assert list(g.get_edge_property("weight").to_numpy()) == [1, 2, 3]


def test_trivial_arrays_unsorted():
    g = from_edge_list_arrays(np.array([0, 10, 1]), np.array([1, 0, 2]))
    assert ranges_all_equivalent(
        [g.out_edge_ids(n) for n in g.nodes()],
        [
            range(0, 1),
            range(1, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 3),
        ],
    )
    assert [g.get_edge_dst(i) for i in range(g.num_edges())] == [1, 2, 0]


def test_typed_arrays_unsorted():
    g = from_edge_list_arrays(
        np.array([0, 10, 1]), np.array([1, 0, 2]), edge_types=EntityTypeArray.from_type_names(["A", "B", "A"])
    )
    edge_types = g.edge_types
    assert g.get_edge_type(0) == edge_types.atomic_types["A"]
    assert g.get_edge_type(1) == edge_types.atomic_types["A"]
    assert g.get_edge_type(2) == edge_types.atomic_types["B"]


def test_multi_typed_arrays():
    g = from_edge_list_arrays(
        np.array([0, 3, 1]),
        np.array([1, 0, 2]),
        node_types=EntityTypeArray.from_type_name_sets([{"A", "B"}, {"B"}, {"A"}, {"A", "C"}]),
    )
    node_types = g.node_types
    assert g.get_node_type(0) == node_types.get_non_atomic_entity_type(
        [node_types.atomic_types["A"], node_types.atomic_types["B"]]
    )
    assert g.does_node_have_type(0, node_types.atomic_types["A"])
    assert g.does_node_have_type(3, node_types.atomic_types["A"])
    assert g.get_node_type(1) == node_types.atomic_types["B"]


def test_trivial_arrays_sorted():
    g = from_sorted_edge_list_arrays(np.array([0, 1, 1, 10]), np.array([1, 2, 1, 0]))
    assert ranges_all_equivalent(
        [g.out_edge_ids(n) for n in g.nodes()],
        [
            range(0, 1),
            range(1, 3),
            range(3, 3),
            range(3, 3),
            range(3, 3),
            range(3, 3),
            range(3, 3),
            range(3, 3),
            range(3, 3),
            range(3, 3),
            range(3, 4),
        ],
    )
    assert [g.get_edge_dst(i) for i in range(g.num_edges())] == [1, 2, 1, 0]


def test_properties_arrays_unsorted():
    g = from_edge_list_arrays(np.array([0, 1, 10, 1]), np.array([1, 2, 0, 2]), prop=np.array([1, 2, 3, 2]))
    assert list(g.get_edge_property("prop").to_numpy()) == [1, 2, 2, 3]


def test_properties_arrays_sorted():
    g = from_sorted_edge_list_arrays(np.array([0, 1, 1, 10]), np.array([1, 2, 1, 0]), prop=np.array([1, 2, 3, 4]))
    assert list(g.get_edge_property("prop").to_numpy()) == [1, 2, 3, 4]


def test_trivial_matrix():
    g = from_edge_list_matrix(np.array([[0, 1], [1, 2], [10, 0]]))
    assert ranges_all_equivalent(
        [g.out_edge_ids(n) for n in g.nodes()],
        [
            range(0, 1),
            range(1, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 3),
        ],
    )
    assert [g.get_edge_dst(i) for i in range(g.num_edges())] == [1, 2, 0]


def test_arrays_bad_arguments():
    with pytest.raises(TypeError):
        from_edge_list_arrays(np.array([[0, 0], [1, 0]]), np.array([1, 2, 0]))
    with pytest.raises(TypeError):
        from_edge_list_arrays(np.array([1, 2, 0]), np.array([[0, 0], [1, 0]]))
    with pytest.raises(ValueError):
        from_edge_list_arrays(np.array([1, 2, 0]), np.array([0, 0, 1, 0]))
    with pytest.raises(ValueError):
        from_edge_list_arrays(np.array([]), np.array([]))


def test_matrix_bad_arguments():
    with pytest.raises(TypeError):
        from_edge_list_matrix(np.array([1, 2, 0]))
    with pytest.raises(TypeError):
        from_edge_list_matrix(np.array([[0, 0, 1], [1, 0, 3]]))


def test_dataframe():
    g = from_edge_list_dataframe(pandas.DataFrame(dict(source=[0, 1, 10], destination=[1, 2, 0], prop=[1, 2, 3])))
    assert ranges_all_equivalent(
        [g.out_edge_ids(n) for n in g.nodes()],
        [
            range(0, 1),
            range(1, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 2),
            range(2, 3),
        ],
    )
    assert [g.get_edge_dst(i) for i in range(g.num_edges())] == [1, 2, 0]
    assert list(g.get_edge_property("prop").to_numpy()) == [1, 2, 3]


def test_typed_dataframe():
    df = pandas.DataFrame(
        dict(source=[0, 1, 10], destination=[1, 2, 0], prop=[1, 2, 3], types=pandas.Categorical(["A", "B", "A"]))
    )
    g = from_edge_list_dataframe(
        df[["source", "destination", "prop"]], edge_types=EntityTypeArray.from_type_names(df["types"])
    )
    assert [g.get_edge_dst(i) for i in range(g.num_edges())] == [1, 2, 0]
    assert [str(g.get_edge_type(i)) for i in range(g.num_edges())] == ["A", "B", "A"]


def test_from_csr():
    pg = from_csr(np.array([1, 1], dtype=np.uint32), np.array([1], dtype=np.uint64))
    assert pg.num_nodes() == 2
    assert pg.num_edges() == 1
    # assert list(pg.out_edge_ids(0)) == [0]
    assert pg.get_edge_dst(0) == 1


def test_from_csr_int16():
    pg = from_csr(np.array([1, 1], dtype=np.int16), np.array([1], dtype=np.int16))
    assert pg.num_nodes() == 2
    assert pg.num_edges() == 1
    # assert list(pg.out_edge_ids(0)) == [0]
    assert pg.get_edge_dst(0) == 1


def test_from_csr_k3():
    pg = from_csr(np.array([2, 4, 6]), np.array([1, 2, 0, 2, 0, 1]))
    assert pg.num_nodes() == 3
    assert pg.num_edges() == 6
    # assert list(pg.out_edge_ids(2)) == [4, 5]
    assert pg.get_edge_dst(4) == 0
    assert pg.get_edge_dst(5) == 1


@pytest.mark.required_env("KATANA_TEST_DATASETS")
def test_load_graphml():
    input_file = Path(get_misc_dataset("graph-convert/movies.graphml"))
    pg = from_graphml(input_file)
    assert pg.get_node_property("name")[1].as_py() == "Keanu Reeves"


@pytest.mark.required_env("KATANA_TEST_DATASETS")
def test_load_graphml_write():
    input_file = Path(get_misc_dataset("graph-convert/movies.graphml"))
    pg = from_graphml(input_file)
    with TemporaryDirectory() as tmpdir:
        pg.write(tmpdir)
        del pg
        graph = Graph(tmpdir)
        assert graph.path == f"file://{tmpdir}"
    assert graph.get_node_property("name")[1].as_py() == "Keanu Reeves"
