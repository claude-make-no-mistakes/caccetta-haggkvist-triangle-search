"""Check an arc list for a directed-triangle counterexample."""

from argparse import ArgumentParser
from dataclasses import asdict, dataclass
import json
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Result:
    order: int
    arc_count: int
    min_outdegree: int
    directed_triangles: int
    first_triangle: tuple[int, int, int] | None
    maximum_step: int
    base_deletions: int | None
    deletions_by_head_residue: tuple[int, ...] | None
    counterexample: bool


def verify(order: int, arcs: Iterable[tuple[int, int]]) -> Result:
    """Use direct adjacency, without trusting a solver's triangle count."""
    if type(order) is not int or order < 1:
        raise ValueError("order must be a positive integer")
    outgoing: list[set[int]] = [set() for _ in range(order)]
    incoming: list[set[int]] = [set() for _ in range(order)]
    for u, v in arcs:
        if type(u) is not int or type(v) is not int or not (0 <= u < order and 0 <= v < order):
            raise ValueError(f"vertex outside the prescribed range in arc {(u, v)}")
        if u == v or v in outgoing[u] or u in outgoing[v]:
            raise ValueError(f"loop, duplicate or reciprocal arc at {(u, v)}")
        outgoing[u].add(v)
        incoming[v].add(u)

    triangles = 0
    first_triangle = None
    maximum_step = 0
    for u, row in enumerate(outgoing):
        for v in sorted(row):
            maximum_step = max(maximum_step, (v - u) % order)
            if u < v:
                # Count each cycle at its unique least vertex, in arc order.
                for w in sorted(outgoing[v] & incoming[u]):
                    if u < w:
                        triangles += 1
                        if first_triangle is None:
                            first_triangle = (u, v, w)

    minimum = min(map(len, outgoing))
    heads = None
    if order % 3 == 0:
        r = order // 3
        heads = [0] * r
        for v in range(order):
            for step in range(1, r):
                if (v - step) % order not in incoming[v]:
                    heads[v % r] += 1
    return Result(
        order, sum(map(len, outgoing)), minimum, triangles, first_triangle, maximum_step,
        sum(heads) if heads is not None else None, tuple(heads) if heads is not None else None,
        minimum >= (order + 2) // 3 and triangles == 0,
    )


def read_arcs(path: Path) -> Iterable[tuple[int, int]]:
    with path.open(encoding="ascii") as stream:
        for line_number, line in enumerate(stream, start=1):
            fields = line.split("#", 1)[0].split()
            if fields:
                if len(fields) != 2:
                    raise ValueError(f"{path}:{line_number} requires two vertex numbers")
                yield int(fields[0]), int(fields[1])


def main() -> int:
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--order", type=int, default=153)
    parser.add_argument("--require-counterexample", action="store_true")
    args = parser.parse_args()
    try:
        result = verify(args.order, read_arcs(args.path))
    except ValueError as exc:
        parser.error(str(exc))
    print(json.dumps(asdict(result)))
    return int(args.require_counterexample and not result.counterexample)


if __name__ == "__main__":
    raise SystemExit(main())
