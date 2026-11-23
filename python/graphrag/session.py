from __future__ import annotations

import re
from dataclasses import dataclass

from graphrag.tools import RunContext

ALGORITHM_LABELS = {"bfs": "BFS", "dfs": "DFS", "dijkstra": "Dijkstra", "bellman-ford": "Bellman-Ford"}

ALGORITHM_PATTERNS = {
    "bfs": r"\bbfs\b",
    "dfs": r"\bdfs\b",
    "dijkstra": r"\bdijkstra\b",
    "bellman-ford": r"\bbellman[\s-]*ford\b",
}


def label(algorithm: str) -> str:
    return ALGORITHM_LABELS.get(algorithm, algorithm)


@dataclass
class QASession:
    contexts: list[RunContext]

    def __post_init__(self) -> None:
        if not self.contexts:
            raise ValueError("Select at least one run")

    @property
    def node_names(self) -> list[str]:
        return list(self.contexts[0].node_names.values())

    def run(self, selector: str | None = None) -> RunContext:
        if not selector:
            return self.contexts[0]
        wanted = selector.strip().lower().replace(" ", "-")
        for ctx in self.contexts:
            if wanted in {ctx.algorithm.lower(), ctx.run_id.lower()}:
                return ctx
        available = ", ".join(ctx.algorithm for ctx in self.contexts)
        raise ValueError(f"No run for {selector!r}; available: {available}")

    def mentioned_algorithms(self, text: str) -> list[RunContext]:
        lowered = text.lower()
        return [ctx for ctx in self.contexts if re.search(ALGORITHM_PATTERNS.get(ctx.algorithm, "$^"), lowered)]

    def mentioned_nodes(self, text: str) -> list[str]:
        name_by_lowercase = {name.lower(): name for name in self.node_names}
        return [name_by_lowercase[word] for word in re.findall(r"[a-z0-9]+", text.lower()) if word in name_by_lowercase]
