#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace {

constexpr int vertices = 153;
constexpr int degree = 51;

int distance(int tail, int head) {
    return (head - tail + vertices) % vertices;
}

struct Graph {
    std::array<std::bitset<vertices>, vertices> outgoing{};
    std::array<std::bitset<vertices>, vertices> incoming{};
    std::array<std::array<int, degree>, vertices> heads{};

    void set_arc(int tail, int head, bool present) {
        outgoing[tail].set(head, present);
        incoming[head].set(tail, present);
    }

    int through_arc(int tail, int head) const {
        return static_cast<int>((outgoing[head] & incoming[tail]).count());
    }
};

Graph circulant() {
    Graph graph;
    for (int tail = 0; tail < vertices; ++tail) {
        for (int step = 1; step <= degree; ++step) {
            const int head = (tail + step) % vertices;
            graph.set_arc(tail, head, true);
            graph.heads[tail][step - 1] = head;
        }
    }
    return graph;
}

void verify(const Graph& graph, int expected_triangles, int expected_insertions) {
    int insertions = 0;
    for (int tail = 0; tail < vertices; ++tail) {
        std::bitset<vertices> listed;
        for (int head : graph.heads[tail]) {
            if (head < 0 || head >= vertices || head == tail || listed[head]) {
                throw std::logic_error("invalid outgoing row at vertex " + std::to_string(tail));
            }
            listed.set(head);
            insertions += distance(tail, head) > degree;
        }
        if (listed != graph.outgoing[tail] || (listed & graph.incoming[tail]).any()) {
            throw std::logic_error("adjacency indexes or orientation disagree at vertex " + std::to_string(tail));
        }
        for (int head = 0; head < vertices; ++head) {
            if (listed[head] != graph.incoming[head][tail]) {
                throw std::logic_error("incoming index disagrees at vertex " + std::to_string(head));
            }
        }
    }
    if (insertions != expected_insertions) {
        throw std::logic_error("incorrect count of arcs added to the circulant");
    }

    // Independent of through_arc and the delta: enumerate each unordered triple once.
    int triangles = 0;
    for (int first = 0; first < vertices; ++first) {
        for (int second = first + 1; second < vertices; ++second) {
            if (graph.outgoing[first][second]) {
                for (int third = second + 1; third < vertices; ++third) {
                    triangles += graph.outgoing[second][third] && graph.outgoing[third][first];
                }
            } else if (graph.outgoing[second][first]) {
                for (int third = second + 1; third < vertices; ++third) {
                    triangles += graph.outgoing[first][third] && graph.outgoing[third][second];
                }
            }
        }
    }
    if (triangles != expected_triangles) {
        throw std::logic_error("triangle delta disagrees with full enumeration: " +
                               std::to_string(expected_triangles) + " versus " + std::to_string(triangles));
    }
}

int sample(std::mt19937& rng, int bound) {
    const auto width = static_cast<std::uint64_t>(bound);
    const auto range = static_cast<std::uint64_t>(std::mt19937::max()) + 1;
    const auto limit = range - range % width;
    std::uint64_t draw = rng();
    while (draw >= limit) {
        draw = rng();
    }
    return static_cast<int>(draw % width);
}

double uniform(std::mt19937& rng) {
    return static_cast<double>(rng()) / (static_cast<double>(std::mt19937::max()) + 1.0);
}

struct Swap {
    int tail;
    int slot;
    int removed;
    int added;
};

struct Move {
    std::array<Swap, 2> swaps{};
    int size = 0;
};

int proposed_head(const Graph& graph, int tail, int band, bool use_long_step,
                  int forbidden, std::mt19937& rng) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        int step;
        if (use_long_step) {
            step = band + 1 + sample(rng, vertices - 1 - band);
        } else if (attempt < 4) {
            const int first = degree - 6;
            const int last = std::min(band, degree + 8);
            step = first + sample(rng, last - first + 1);
        } else {
            step = 1 + sample(rng, band);
        }
        const int head = (tail + step) % vertices;
        if (head != forbidden && !graph.outgoing[tail][head] &&
            (forbidden < 0 || !graph.incoming[tail][head])) {
            return head;
        }
    }
    return -1;
}

int removal_slot(const Graph& graph, int tail, std::mt19937& rng) {
    int slot = sample(rng, degree);
    const int draws = sample(rng, 5) == 0 ? 1 : 8;
    int score = graph.through_arc(tail, graph.heads[tail][slot]);
    for (int draw = 1; draw < draws; ++draw) {
        const int candidate = sample(rng, degree);
        const int candidate_score = graph.through_arc(tail, graph.heads[tail][candidate]);
        if (candidate_score > score) {
            slot = candidate;
            score = candidate_score;
        }
    }
    return slot;
}

Move propose(const Graph& graph, int band, int long_percent, std::mt19937& rng) {
    const int tail = sample(rng, vertices);
    const bool long_step = sample(rng, 100) < long_percent;
    const int head = proposed_head(graph, tail, band, long_step, -1, rng);
    if (head < 0) {
        return {};
    }

    Move move;
    const int slot = removal_slot(graph, tail, rng);
    move.swaps[0] = {tail, slot, graph.heads[tail][slot], head};
    move.size = 1;
    if (graph.outgoing[head][tail]) {
        // Reversing a pair also replaces the old tail's lost outgoing arc.
        const bool second_long_step = sample(rng, 100) < long_percent;
        const int replacement = proposed_head(graph, head, band, second_long_step, tail, rng);
        if (replacement < 0) {
            return {};
        }
        const auto& row = graph.heads[head];
        const int second_slot = static_cast<int>(std::find(row.begin(), row.end(), tail) - row.begin());
        if (second_slot == degree) {
            throw std::logic_error("reverse arc is absent from its outgoing row");
        }
        move.swaps[1] = {head, second_slot, tail, replacement};
        move.size = 2;
    }
    return move;
}

int apply(Graph& graph, const Move& move) {
    int delta = 0;
    // Change arcs serially so a cycle meeting both changed rows is counted once.
    for (int i = 0; i < move.size; ++i) {
        const Swap& swap = move.swaps[i];
        delta -= graph.through_arc(swap.tail, swap.removed);
        graph.set_arc(swap.tail, swap.removed, false);
    }
    for (int i = 0; i < move.size; ++i) {
        const Swap& swap = move.swaps[i];
        delta += graph.through_arc(swap.tail, swap.added);
        graph.set_arc(swap.tail, swap.added, true);
    }
    return delta;
}

void reject(Graph& graph, const Move& move) {
    for (int i = 0; i < move.size; ++i) {
        const Swap& swap = move.swaps[i];
        graph.set_arc(swap.tail, swap.added, false);
    }
    for (int i = 0; i < move.size; ++i) {
        const Swap& swap = move.swaps[i];
        graph.set_arc(swap.tail, swap.removed, true);
    }
}

void save_witness(const Graph& graph, const std::string& prefix, std::uint32_t seed,
                  std::uint64_t trial) {
    const std::string path = prefix + "-" + std::to_string(seed) + "-" + std::to_string(trial) + ".edges";
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot create witness file " + path);
    }
    for (int tail = 0; tail < vertices; ++tail) {
        for (int head = 0; head < vertices; ++head) {
            if (graph.outgoing[tail][head]) {
                output << tail << ' ' << head << '\n';
            }
        }
    }
    output.close();
    if (!output) {
        throw std::runtime_error("cannot finish witness file " + path);
    }
    std::cout << "witness=" << path << '\n';
}

void run(std::uint32_t seed, std::uint64_t budget, int band, int long_percent,
         const std::string& witness_prefix) {
    std::mt19937 rng(seed);
    Graph graph = circulant();
    int triangles = degree;
    int insertions = 0;
    // Three positive steps at most 51 sum to 153 only if all three equal 51.
    // The step-51 arcs partition the 153 vertices into 51 directed 3-cycles.
    verify(graph, degree, 0);

    Graph best_graph = graph;
    int best = triangles;
    int best_insertions = 0;
    int best_perturbed = std::numeric_limits<int>::max();
    std::uint64_t feasible = 0;
    std::uint64_t accepted = 0;
    std::uint64_t repaired = 0;
    std::uint64_t accepted_beyond_band = 0;
    std::uint64_t trial = 0;
    std::cout << "seed=" << seed << " budget=" << budget << " band=" << band
              << " long_percent=" << long_percent << " baseline=" << triangles << '\n';

    const std::array<double, 4> initial_temperatures{{1.0, 3.0, 9.0, 22.0}};
    for (std::size_t round = 0; round < initial_temperatures.size() && best > 0; ++round) {
        graph = best_graph;
        triangles = best;
        insertions = best_insertions;
        int round_best = triangles;
        const auto round_count = static_cast<std::uint64_t>(initial_temperatures.size());
        const std::uint64_t steps = budget / round_count + (round < budget % round_count);
        const std::uint64_t cooling_steps = steps - steps / 5;
        const double initial_temperature = initial_temperatures[round];
        for (std::uint64_t step = 0; step < steps && best > 0; ++step) {
            ++trial;
            const Move move = propose(graph, band, long_percent, rng);
            if (move.size > 0) {
                ++feasible;
                const int delta = apply(graph, move);
                const double progress = static_cast<double>(step) / static_cast<double>(cooling_steps);
                const double temperature = initial_temperature * std::pow(0.04 / initial_temperature, progress);
                if (delta <= 0 || (step < cooling_steps && uniform(rng) < std::exp(-delta / temperature))) {
                    bool beyond_band = false;
                    for (int i = 0; i < move.size; ++i) {
                        const Swap& swap = move.swaps[i];
                        graph.heads[swap.tail][swap.slot] = swap.added;
                        insertions += (distance(swap.tail, swap.added) > degree) -
                                      (distance(swap.tail, swap.removed) > degree);
                        beyond_band |= distance(swap.tail, swap.added) > band;
                    }
                    ++accepted;
                    repaired += move.size == 2;
                    accepted_beyond_band += beyond_band;
                    triangles += delta;
                    round_best = std::min(round_best, triangles);
                    if (insertions > 0 && triangles < best_perturbed) {
                        verify(graph, triangles, insertions);
                        best_perturbed = triangles;
                    }
                    if (triangles < best) {
                        verify(graph, triangles, insertions);
                        best = triangles;
                        best_graph = graph;
                        best_insertions = insertions;
                        std::cout << "improved=" << best << " trial=" << trial
                                  << " differing_arcs=" << insertions << '\n';
                        if (best == 0) {
                            save_witness(best_graph, witness_prefix, seed, trial);
                        }
                    }
                } else {
                    reject(graph, move);
                }
            }
            if (trial % 8192 == 0) {
                verify(graph, triangles, insertions);
            }
        }
        verify(graph, triangles, insertions);
        std::cout << "round=" << round + 1 << " trials=" << trial << " round_best=" << round_best
                  << " final=" << triangles << " differing_arcs=" << insertions << '\n';
    }
    std::cout << "best=" << best << " best_perturbed=";
    if (best_perturbed == std::numeric_limits<int>::max()) {
        std::cout << "none";
    } else {
        std::cout << best_perturbed;
    }
    std::cout << " trials=" << trial << " feasible=" << feasible << " accepted=" << accepted
              << " reverse_repairs=" << repaired << " accepted_beyond_band=" << accepted_beyond_band << '\n';
}

std::uint64_t parse(const char* argument, std::uint64_t minimum, std::uint64_t maximum) {
    const std::string value(argument);
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
        throw std::invalid_argument("expected an unsigned decimal argument, got " + value);
    }
    const auto parsed = std::stoull(value);
    if (parsed < minimum || parsed > maximum) {
        throw std::invalid_argument(value + " is outside [" + std::to_string(minimum) + ", " +
                                    std::to_string(maximum) + "]");
    }
    return parsed;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5 && argc != 6) {
        std::cerr << "usage: targeted SEED PROPOSALS BAND LONG_PERCENT [WITNESS_PREFIX]\n"
                  << "BAND is the maximum forward step for ordinary proposals (52 through 151).\n"
                  << "LONG_PERCENT is the share of proposals drawing forward steps above BAND.\n"
                  << "Witness files contain zero-based edges, one tail and head per line.\n";
        return 2;
    }

    try {
        const auto seed = static_cast<std::uint32_t>(parse(argv[1], 0, std::numeric_limits<std::uint32_t>::max()));
        const std::uint64_t budget = parse(argv[2], 4, std::numeric_limits<std::uint64_t>::max());
        const int band = static_cast<int>(parse(argv[3], degree + 1, vertices - 2));
        const int long_percent = static_cast<int>(parse(argv[4], 0, 100));
        run(seed, budget, band, long_percent, argc == 6 ? argv[5] : "targeted-zero");
    } catch (const std::exception& error) {
        std::cerr << "targeted: " << error.what() << '\n';
        return 1;
    }
}
