# Caccetta–Häggkvist directed-triangle search

The conjecture asks whether every oriented graph of order $n$ with minimum
outdegree at least $\lceil n/3\rceil$ has a directed triangle. The search
found no counterexample. The public SAT campaign reports order 153 as its
first remaining order. [Campaign log](https://github.com/wustep/maths/blob/main/problems/caccetta-haggkvist-k3/ATTACK.md)

The [paper](note.pdf) proves two restrictions on a possible counterexample.

- On $3r$ vertices with $r\ge3$, any counterexample must omit at least $r$
  of the arcs of forward lengths $1,\ldots,r-1$ in every cyclic labeling.
  At order 153 this requires at least 51 deletions and 204 additions
  relative to the base graph. Keeping all base arcs forces at least 51
  triangles, and that bound is attained.
- For every $n\ge15$, a counterexample needs a forward arc longer than
  $\lceil n/3\rceil+2$ in every cyclic labeling. The proof uses a
  15-vertex obstruction with 105 Boolean variables and 605 clauses. The
  local [DRAT certificate](certificates/strip-5-2.drat) was checked
  using DRAT-trim.

## Replay the Boolean certificates

Use Python 3.10 or later, [DRAT-trim](https://github.com/marijnheule/drat-trim),
and the pinned dependency in [requirements.txt](requirements.txt) for the
full-order SAT search. Generating the local obstruction uses the standard
library.

~~~sh
python strip.py --width 5 --s 2 --cnf strip.cnf
drat-trim strip.cnf certificates/strip-5-2.drat

python band.py --r 51 --s 2 --cnf band.cnf
drat-trim band.cnf certificates/band-153-2.drat
~~~

The [full-order band](band.py) allows steps from 1 through $r+s$ on
$3r$ vertices. Its order-153 formulas for $s=0,1,2$ returned UNSAT and
their traces passed DRAT-trim. The $s=2$ trace is archived here. The
[full-order run log](band-runs.jsonl) includes the inconclusive cases
$s=3,4$, which reached budgets of 300,000 and 100,000 conflicts.

## Replay the perturbation search

The [C++ search](targeted.cpp) keeps each outdegree at 51 and counts
directed triangles exactly. It checks improvements by enumerating triples
independently. The starting graph has all forward steps 1 through 51
and has 51 directed triangles. Across 2.4 million proposals, the best
changed graph had 52. The [run log](targeted-runs.txt) contains each seed
and its proposal parameters.

~~~sh
g++ -std=c++14 -O3 -Wall -Wextra -Wconversion -pedantic targeted.cpp -o targeted
./targeted 101 300000 52 0
./targeted 108 300000 128 25
~~~

The arguments are seed, proposal limit, ordinary forward-length cap,
and percentage of proposals that may draw from above that cap. If a run
finds a zero-triangle graph, it writes its arc list. Check any candidate
with the independent [verifier](verify.py).

~~~sh
python verify.py candidate.txt --order 153 --require-counterexample
~~~

The verifier checks vertex ranges, orientation, outdegree and directed
triangles. Arc lists use one zero-indexed tail and head per line.

## Prior art and release

The scope of these results is the directed-triangle case. Hamidoune's
vertex-symmetric theorem covers a different structural hypothesis. The
public announcement on X concerns fixed outdegree six. The literature
and X searches located partial results. The general directed-triangle
conjecture remains open. [Published general bound](https://arxiv.org/abs/0908.2791) · [Vertex-symmetric case](https://arxiv.org/abs/0704.0459) · [X announcement](https://x.com/jjpcodes/status/2079543801387815133)

[Paper source](note.tex) · [Draft post for X](announcement.md)
