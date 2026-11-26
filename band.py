"""Exact triangle-free digraph search in a band of clockwise offsets."""

import argparse
from dataclasses import dataclass
from itertools import combinations
import json
from pathlib import Path
from time import perf_counter

from pysat.card import CardEnc, EncType
from pysat.formula import CNF, IDPool
from pysat.solvers import Solver


@dataclass(frozen=True)
class Band:
    """Order 3r, minimum outdegree r, and allowed offsets 1 through r+s."""

    r: int
    s: int

    def __post_init__(self) -> None:
        if type(self.r) is not int or self.r < 1:
            raise ValueError(f"r must be a positive integer, got {self.r!r}")
        if type(self.s) is not int or not 0 <= 2 * self.s < self.r:
            raise ValueError(f"s must be an integer satisfying 0 <= 2s < r, got {self.s!r}")

    @property
    def n(self) -> int:
        return 3 * self.r

    @property
    def low(self) -> int:
        return self.r - 2 * self.s

    @property
    def high(self) -> int:
        return self.r + self.s

    @property
    def width(self) -> int:
        return self.high - self.low + 1

    def omission(self, vertex: int, offset: int) -> int:
        return vertex * self.width + offset - self.low + 1


def build_band_cnf(r: int, s: int) -> CNF:
    """Encode triangle exclusion and a maximum of s omitted arcs per vertex."""
    band = Band(r, s)
    cnf = CNF()
    pool = IDPool(start_from=band.n * band.width + 1)
    for vertex in range(band.n):
        row = [band.omission(vertex, offset) for offset in range(band.low, band.high + 1)]
        if s <= 1:
            cnf.extend([-lit for lit in excess] for excess in combinations(row, s + 1))
        else:
            cnf.extend(CardEnc.atmost(
                lits=row, bound=s, vpool=pool, encoding=EncType.seqcounter,
            ).clauses)

    # Positive clockwise offsets sum to 3r in any directed triangle since
    # each offset is less than n/2. Each is therefore at least r-2s,
    # and shorter arcs may all be kept. Start each triangle at its least vertex.
    for first_offset in range(band.low, band.high + 1):
        for second_offset in range(band.low, band.high + 1):
            third_offset = band.n - first_offset - second_offset
            if band.low <= third_offset <= band.high:
                for first in range(third_offset):
                    second = first + first_offset
                    third = second + second_offset
                    cnf.append([
                        band.omission(first, first_offset),
                        band.omission(second, second_offset),
                        band.omission(third, third_offset),
                    ])
    return cnf


def validate_candidate(r: int, s: int, omissions: set[tuple[int, int]]) -> int:
    """Rebuild the graph and check every directed two-step walk for a triangle."""
    band = Band(r, s)
    for vertex, offset in omissions:
        if not 0 <= vertex < band.n or not band.low <= offset <= band.high:
            raise ValueError(f"invalid omitted arc (vertex={vertex}, offset={offset})")

    successors = [
        {(vertex + offset) % band.n for offset in range(1, band.high + 1)
         if (vertex, offset) not in omissions}
        for vertex in range(band.n)
    ]
    min_outdegree = min(map(len, successors))
    if min_outdegree < r:
        raise ValueError(f"candidate minimum outdegree is {min_outdegree}, expected at least {r}")
    for first, neighbors in enumerate(successors):
        for second in neighbors:
            for third in successors[second]:
                if first in successors[third]:
                    raise ValueError(f"candidate contains directed triangle {first}, {second}, {third}")
    return min_outdegree


def solve_band(
    r: int,
    s: int,
    *,
    solver: str = "cadical195",
    conflict_budget: int | None = None,
    cnf_path: Path | None = None,
    proof_path: Path | None = None,
) -> dict:
    """Solve one case and optionally write the DIMACS formula and UNSAT proof."""
    band = Band(r, s)
    if conflict_budget is not None and (type(conflict_budget) is not int or conflict_budget < 1):
        raise ValueError(f"conflict budget must be a positive integer, got {conflict_budget!r}")
    if cnf_path is not None and proof_path is not None and cnf_path.resolve() == proof_path.resolve():
        raise ValueError("CNF and proof must use different paths")

    cnf = build_band_cnf(r, s)
    if cnf_path is not None:
        cnf.to_file(str(cnf_path))

    record = {
        "n": band.n,
        "r": r,
        "s": s,
        "solver": solver,
        "conflict_budget": conflict_budget,
        "omission_variables": band.n * band.width,
        "variables": cnf.nv,
        "clauses": len(cnf.clauses),
    }
    with Solver(name=solver, bootstrap_with=cnf, with_proof=proof_path is not None) as search:
        started = perf_counter()
        if conflict_budget is None:
            result = search.solve()
        else:
            search.conf_budget(conflict_budget)
            result = search.solve_limited()
        record["solve_seconds"] = perf_counter() - started
        record["solver_stats"] = search.accum_stats()
        # Drain PySAT's proof stream before closing the backend, even after
        # an inconclusive budgeted solve.
        proof = search.get_proof() if proof_path is not None else None

        if result is True:
            omitted_indices = {
                lit - 1 for lit in search.get_model() if 0 < lit <= band.n * band.width
            }
            omissions = set()
            for index in omitted_indices:
                vertex, offset_index = divmod(index, band.width)
                omissions.add((vertex, offset_index + band.low))
            record["result"] = "sat"
            record["min_outdegree"] = validate_candidate(r, s, omissions)
            record["omitted_arcs"] = sorted(omissions)
        elif result is False:
            record["result"] = "unsat"
            if proof_path is not None:
                if not proof:
                    raise RuntimeError(f"{solver} returned UNSAT without a proof trace")
                with proof_path.open("w", encoding="ascii", newline="\n") as handle:
                    for clause in proof:
                        handle.write(clause + "\n")
                record["proof_lines"] = len(proof)
        else:
            record["result"] = "unresolved"
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r", type=int, required=True, help="a third of the order and the target outdegree")
    parser.add_argument("--s", type=int, required=True, help="width increase beyond offset r")
    parser.add_argument("--solver", default="cadical195", help="PySAT solver name (default: %(default)s)")
    parser.add_argument("--conflict-budget", type=int, help="stop after approximately this many conflicts")
    parser.add_argument("--cnf", type=Path, help="write the DIMACS formula")
    parser.add_argument("--proof", type=Path, help="write the trace if the result is UNSAT")
    args = parser.parse_args()
    try:
        record = solve_band(
            args.r, args.s, solver=args.solver, conflict_budget=args.conflict_budget,
            cnf_path=args.cnf, proof_path=args.proof,
        )
    except ValueError as exc:
        parser.error(str(exc))
    print(json.dumps(record, sort_keys=True))


if __name__ == "__main__":
    main()
