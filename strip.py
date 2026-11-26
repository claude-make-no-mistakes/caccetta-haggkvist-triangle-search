"""Generate a finite residue-strip obstruction for the forward-band search."""

from argparse import ArgumentParser
from itertools import combinations
import json
from pathlib import Path


def strip_formula(width: int, s: int) -> tuple[int, list[list[int]]]:
    if type(width) is not int or width < 1 or type(s) is not int or s < 0:
        raise ValueError("width must be positive and s must be nonnegative")
    deviations = range(-2 * s, s + 1)
    row_size = len(deviations)

    def omit(residue, layer, deviation):
        return (3 * residue + layer) * row_size + deviation + 2 * s + 1

    clauses = []
    for residue in range(width):
        for layer in range(3):
            row = [omit(residue, layer, d) for d in deviations]
            clauses.extend([-lit for lit in group] for group in combinations(row, s + 1))

    # List a triangle by its vertices in layers 0, 1, 2. The layer returns
    # to 0, so the three differences must sum to zero.
    for first in range(width):
        for second in range(width):
            for third in range(width):
                steps = [second - first, third - second, first - third]
                if all(step in deviations for step in steps):
                    clauses.append([omit(first, 0, steps[0]), omit(second, 1, steps[1]),
                                    omit(third, 2, steps[2])])

    return 3 * width * row_size, clauses


def write_dimacs(path: Path, variables: int, clauses: list[list[int]]) -> None:
    with path.open("w", encoding="ascii", newline="\n") as handle:
        handle.write(f"p cnf {variables} {len(clauses)}\n")
        for clause in clauses:
            handle.write(" ".join(map(str, clause)) + " 0\n")


def main() -> None:
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--s", type=int, required=True)
    parser.add_argument("--cnf", type=Path, required=True)
    parser.add_argument("--solve", action="store_true")
    parser.add_argument("--solver", default="cadical195")
    parser.add_argument("--conflicts", type=int)
    parser.add_argument("--proof", type=Path)
    args = parser.parse_args()
    if args.conflicts is not None and args.conflicts < 1:
        parser.error("--conflicts must be positive")
    if args.proof is not None and args.cnf.resolve() == args.proof.resolve():
        parser.error("use separate CNF and proof paths")
    try:
        variables, clauses = strip_formula(args.width, args.s)
        write_dimacs(args.cnf, variables, clauses)
    except ValueError as exc:
        parser.error(str(exc))
    result = {"width": args.width, "s": args.s, "variables": variables, "clauses": len(clauses)}
    if args.solve or args.conflicts is not None or args.proof is not None:
        from pysat.solvers import Solver

        with Solver(name=args.solver, bootstrap_with=clauses, with_proof=args.proof is not None) as solver:
            if args.conflicts is None:
                solved = solver.solve()
            else:
                solver.conf_budget(args.conflicts)
                solved = solver.solve_limited()
            result["result"] = "sat" if solved is True else "unsat" if solved is False else "unresolved"
            result["solver"] = args.solver
            result["conflicts"] = solver.accum_stats()["conflicts"]
            if args.proof is not None:
                proof = solver.get_proof()
                if solved is False:
                    if not proof:
                        raise RuntimeError("solver did not return an UNSAT proof")
                    args.proof.write_text("\n".join(proof) + "\n", encoding="ascii")
                    result["proof_lines"] = len(proof)
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
