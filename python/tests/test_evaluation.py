from graphrag.evaluation import score_text

NAMES = {"PC1", "PC2", "R1", "R2", "R3", "R4"}
ALGOS = ["bfs", "dfs", "dijkstra", "bellman-ford"]


def test_tie_must_be_stated_and_single_winner_named_first():
    tie = ["bfs", "dijkstra"]
    assert score_text("best_algorithm", tie, "BFS and Dijkstra tied at 6.3 ms.", ALGOS, NAMES)
    assert not score_text("best_algorithm", tie, "Bellman-Ford had the lowest latency.", ALGOS, NAMES)
    assert score_text("best_algorithm", ["dfs"], "DFS was fastest; Dijkstra came second.", ALGOS, NAMES)
    assert not score_text("best_algorithm", ["dfs"], "Dijkstra beat DFS.", ALGOS, NAMES)


def test_failure_needs_the_element_named_in_the_failure_clause():
    expected = (["R1", "R2"], 3.0)
    assert score_text("first_failure", expected, "The R1–R2 link went down at 3 s.", ALGOS, NAMES)
    assert not score_text(
        "first_failure", expected, "LINK_DOWN on 2 at t=3.000s; paths changed (R1 -> R2 -> R3).", ALGOS, NAMES
    )


def test_paths_must_match_hop_by_hop():
    path = ["PC1", "R1", "R4", "R3", "PC2"]
    assert score_text("flow_path", path, "Traffic went PC1 → R1 → R4 → R3 → PC2.", ALGOS, NAMES)
    assert not score_text("flow_path", path, "PC1 -> R1 -> R3 -> PC2", ALGOS, NAMES)


def test_drop_reason_needs_reason_and_count():
    assert score_text("drop_reason", ("no_route", 2010), "2010 packets were dropped: no route to PC2.", ALGOS, NAMES)
    assert not score_text("drop_reason", ("no_route", 2010), "Packets were dropped because R3 failed.", ALGOS, NAMES)


def test_drop_reason_accepts_plain_wording_and_grouped_numbers():
    answer = "Packets could not find a route – 2 010 were dropped in total."
    assert score_text("drop_reason", ("no_route", 2010), answer, ALGOS, NAMES)
    assert not score_text("drop_reason", ("queue_full", 2010), answer, ALGOS, NAMES)
