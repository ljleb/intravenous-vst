#pragma once
#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <span>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <any>

enum struct PolyblepSide {
    LEFT,
    RIGHT,
};

float polyblep_phi(float const& sample, float const& warp_threshold) {
    auto const res = (sample / warp_threshold + 1.f) / 2.f;
    if (!std::isfinite(res)) return 0.5;
    return res;
}

float polyblep_p(float const& phi, float const& delta, PolyblepSide side) {
    if (side == PolyblepSide::RIGHT && phi < delta) {
        auto const& first_order = 2.f * phi / delta;
        auto const& second_order = phi / delta;
        return first_order - second_order * second_order - 1;
    }
    if (side == PolyblepSide::LEFT && 1 - delta <= phi) {
        auto const& second_order = (phi - 1) / delta + 1;
        return second_order * second_order;
    }
    return 0;
}

float polyblep_error(float sample, float delta, float warp_threshold, PolyblepSide side) {
    float sign = std::signbit(delta);
    delta = std::abs(delta) / warp_threshold;

    float const phi = polyblep_phi(sample, warp_threshold);
    float const p = polyblep_p(phi, delta, side) * warp_threshold * sign;
    if (!std::isfinite(p)) return 0.f;
    return p;
}

namespace iv {
    using Sample = float;

    /* ─────────────  Ports  ───────────── */
    struct InputPort {
        std::vector<Sample> _buffer;
        size_t _write_idx{0};

        InputPort() noexcept {
            _buffer.push_back(0);
        }

        InputPort(InputPort const&) = delete;
        InputPort(InputPort&& other) noexcept {
            *this = std::move(other);
        }

        InputPort& operator=(InputPort const&) = delete;
        InputPort& operator=(InputPort&& other) noexcept {
            _buffer = std::move(other._buffer);
            _write_idx = other._write_idx;
            return *this;
        }

        Sample next() noexcept {
            _write_idx = (_write_idx + 1) % _buffer.size();
            Sample v = _buffer[_write_idx];
            _buffer[_write_idx] = 0.f;
            return v;
        }

        void update(size_t offset, Sample value) noexcept {
            // update the current position in the buffer
            // offset brings the write head closer to a real time update
            size_t idx = (_write_idx + _buffer.size() - offset) % _buffer.size();
            _buffer[idx] = value;
        }

        void add_latency(size_t latency) noexcept {
            _buffer.assign(_buffer.size() + latency, 0);
            _buffer.shrink_to_fit();
        }

        size_t get_latency() const noexcept {
            return _buffer.size() - 1;
        }
    };

    struct OutputPort {
        size_t _latency;
        std::vector<InputPort*> fan;
        std::vector<Sample> _buffer;
        size_t _index;

        OutputPort(size_t latency = 0): _latency(latency), _buffer(latency + 1), _index(0) {}

        void push(Sample v) noexcept {
            next();
            update(0, v);
        }

        void update(size_t offset, Sample value) noexcept {
            if (offset > _latency) return;
            size_t idx = (_index + _buffer.size() - offset) % _buffer.size();
            _buffer[idx] = value;
            for (auto* dst : fan) {
                dst->update(offset, value);
            }
        }

        void next() noexcept {
            _index = (_index + 1) % _buffer.size();
            _buffer[_index] = 0;
        }

        Sample back(size_t offset = 0) const noexcept {
            if (offset > _latency) return 0;
            size_t idx = (_index + _buffer.size() - offset) % _buffer.size();
            return _buffer[idx];
        }

        size_t latency() const noexcept {
            return _latency;
        }
    };

    /* ─────────────  Node base  ───────────── */
    struct Node {
        virtual ~Node() = default;
        virtual void tick(std::span<juce::MidiMessage> const& midi) noexcept = 0;
        virtual std::span<InputPort const> inputs() const noexcept = 0;
        virtual std::span<OutputPort const> outputs() const noexcept = 0;

        virtual std::span<InputPort> inputs() noexcept {
            auto span = const_cast<Node const*>(this)->inputs();
            return { const_cast<InputPort*>(span.data()), span.size(), };
        }

        virtual std::span<OutputPort> outputs() noexcept {
            auto span = const_cast<Node const*>(this)->outputs();
            return { const_cast<OutputPort*>(span.data()), span.size(), };
        }
    };

    /* ─────────────  Helpers  ───────────── */
    static inline iv::Sample warp_pm1(Sample x, Sample threshold) {
        if (x > threshold)  return std::fmod(x + threshold, 2 * threshold) - threshold;
        if (x < -threshold) return -std::fmod(-x + threshold, 2 * threshold) + threshold;
        return x;
    }

    /* ─────────────  Concrete nodes  ───────────── */
    struct SumNode : public Node {
        std::vector<InputPort> ins;
        OutputPort out;

        SumNode(std::vector<InputPort>&& ins): ins(std::move(ins)) {}

        void tick(std::span<juce::MidiMessage> const& midi) noexcept override {
            Sample result = 0;
            for (auto& in : ins) {
                result += in.next();
            }
            out.push(result);
        }
        std::span<InputPort const> inputs() const noexcept override { return ins; }
        std::span<OutputPort const> outputs() const noexcept override { return { &out, 1 }; }
    };

    struct IntegratorNode : public Node {
        InputPort in_vel, in_prev, in_sample_rate;
        OutputPort out;

        void tick(std::span<juce::MidiMessage> const& midi) noexcept override {
            out.push(in_vel.next() / in_sample_rate.next() + in_prev.next());
        }
        std::span<InputPort const> inputs() const noexcept override { return { &in_vel, 3 }; }
        std::span<OutputPort const> outputs() const noexcept override { return { &out, 1 }; }
    };

    struct WarperNode : public Node {
        InputPort  in, in_threshold;
        OutputPort out, out_aa{1};

        void tick(std::span<juce::MidiMessage> const& midi) noexcept override {
            Sample threshold = in_threshold.next();
            Sample sample_prev = out.back();
            Sample sample = in.next();
            Sample sample_warped = sample;
            bool warped = false;
            
            if (sample > threshold) { sample_warped = warp_pm1(sample, threshold); warped = true; }
            else if (sample < -threshold) { sample_warped = warp_pm1(sample, threshold); warped = true; }
            out.push(sample_warped);

            Sample sample_warped_aa = sample_warped;
            if (warped) {
                Sample delta = (sample - sample_prev) / 2;
                sample_warped_aa -= polyblep_error(sample_warped_aa, delta, threshold, PolyblepSide::RIGHT);
                out_aa.update(0, sample_prev - polyblep_error(sample_prev, delta, threshold, PolyblepSide::LEFT));
            }
            out_aa.push(sample_warped_aa);
        }
        std::span<InputPort> inputs() noexcept override { return { &in, 2 }; }
        std::span<OutputPort> outputs() noexcept override { return { &out, 2 }; }
    };

    /* ─────────────  Patch  ───────────── */
    class Graph : public Node {
        std::vector<std::unique_ptr<Node>> _nodes;

    public:
        std::vector<InputPort> ins;
        std::vector<OutputPort> outs;

        Graph(
            decltype(_nodes) nodes = {},
            decltype(ins) ins = {},
            decltype(outs) outs = {}
        ):
            _nodes(std::move(nodes)),
            ins(std::move(ins)),
            outs(outs)
        {
            topological_sort_with_cycles();
            init_buffers();
        }

        /*  ▄▄▄▄▄  Graph::topological_sort_with_cycles  ▄▄▄▄▄
            Visits every node exactly once in *breadth‑first*
            order, starting from the nodes that are reached
            first when you fan‑out from the graph’s external
            outputs, as well as nodes with no inupts.
        */
        void topological_sort_with_cycles()
        {
            const size_t n = _nodes.size();

            /* — map every InputPort* back to its owner node index — */
            std::unordered_map<InputPort*, size_t> owner;
            for (size_t i = 0; i < n; ++i) {
                for (auto& in : _nodes[i]->inputs()) {
                    owner[&in] = i;
                }
            }

            /* — breadth‑first order — */
            std::deque<size_t> queue;

            /* 1. seed with every node that has *no* inputs (true sources) */
            for (size_t i = 0; i < n; ++i) {
                if (_nodes[i]->inputs().empty()) {
                    queue.push_back(i);
                }
            }

            /* 2. seed with nodes that read directly from *graph* inputs
                  (i.e. any consumer of an OutputPort in `outs`)          */
            for (auto& out : outs) {
                for (auto* dst : out.fan) {
                    if (auto it = owner.find(dst); it != owner.end()) {
                        queue.push_back(it->second);
                    }
                }
            }

            std::vector<bool> seen(n, false);
            std::vector<std::unique_ptr<Node>> sorted;
            sorted.reserve(n);

            while (!queue.empty()) {
                size_t node = queue.front();
                queue.pop_front();

                if (seen[node]) continue;           // already scheduled via another path
                seen[node] = true;

                /* push every consumer of v */
                for (auto& out : _nodes[node]->outputs()) {                 // for each output of the node
                    for (auto* dst : out.fan) {                             // for each input connected to the output
                        if (auto it = owner.find(dst); it != owner.end()) { // get the node the input is from
                            size_t child = it->second;
                            if (!seen[child]) queue.push_back(child);       // chad node, we keep those
                        }
                    }
                }
                sorted.emplace_back(std::move(_nodes[node]));
            }

            _nodes.swap(sorted);   // new deterministic execution order, cycles allowed
        }

        void init_buffers()
        {
            std::unordered_map<InputPort const*, size_t> input_global_latencies;

            for (size_t node = 0; node < _nodes.size(); ++node) {
                size_t node_global_latency = 0;
                for (auto& in : _nodes[node]->inputs()) {
                    node_global_latency = std::max(node_global_latency, input_global_latencies[&in]);
                }
                for (auto& in : _nodes[node]->inputs()) {
                    in.add_latency(node_global_latency - input_global_latencies[&in]);
                }
                for (auto& out : _nodes[node]->outputs()) {
                    for (auto& dst : out.fan) {
                        dst->add_latency(out.latency());
                        input_global_latencies[dst] = node_global_latency + out.latency();
                    }
                }
            }
        }

        void tick(std::span<juce::MidiMessage> const& midi) noexcept override { for (auto& node : _nodes) node->tick(midi); }
        std::span<InputPort const> inputs() const noexcept override { return ins; }
        std::span<OutputPort const> outputs() const noexcept override { return outs; }

        size_t compute_latency() const {
            std::unordered_map<InputPort const*, size_t> arrival;
            size_t graph_latency = 0;

            for (auto const& node : _nodes) {
                size_t here = 0;
                for (auto const& in : node->inputs()) {
                    here = std::max(here, arrival[&in]);
                }

                for (auto const& out : node->outputs()) {
                    size_t out_latency = here + out.latency();
                    for (auto* dst : out.fan) {
                        arrival[dst] = std::max(arrival[dst], out_latency);
                    }
                }
            }

            for (auto const& sink : ins) {
                graph_latency = std::max(graph_latency, arrival[&sink]);
            }
            for (auto const& node : _nodes) {
                if (node->outputs().empty()) {
                    for (auto const& sink : node->inputs()) {
                        graph_latency = std::max(graph_latency, arrival[&sink]);
                    }
                }
            }

            return graph_latency;
        }
    };

    struct NodeFactoryBase {
        virtual std::unique_ptr<Node> create() const = 0;
        virtual ~NodeFactoryBase() = default;
    };

    template<class T>
    struct NodeFactory : public NodeFactoryBase {
        std::unique_ptr<Node> create() const override {
            return std::make_unique<T>();
        }
    };

    template<>
    struct NodeFactory<SumNode> : public NodeFactoryBase {
        size_t num_inputs{0};

        std::unique_ptr<Node> create() const override
        {
            std::vector<InputPort> inputs(num_inputs);
            return std::make_unique<SumNode>(std::move(inputs));
        }

        size_t add_input_port() noexcept {
            return num_inputs++;
        }
    };

    template<>
    struct NodeFactory<Graph> : public NodeFactoryBase {
        static const size_t GRAPH_ID = std::numeric_limits<size_t>::max();

        struct PortId { size_t node, port; };
        struct Edge { PortId source, destination; };

        std::vector<std::unique_ptr<NodeFactoryBase>> factories;
        std::vector<Edge> edges;
        size_t num_inputs{0}, num_outputs{0};

        std::unique_ptr<Node> create() const override
        {
            std::vector<std::unique_ptr<Node>> nodes;
            std::unordered_set<InputPort const*> seen_inputs;

            std::vector<InputPort> graph_inputs(num_inputs);
            std::vector<OutputPort> graph_outputs(num_outputs);

            for (auto& factory : factories) {
                nodes.emplace_back(factory->create());
            }

            for (auto edge : edges) {
                std::span<InputPort> inputs = (edge.destination.node == NodeFactory<Graph>::GRAPH_ID)
                    ? graph_inputs
                    : nodes[edge.destination.node]->inputs();
                std::span<OutputPort> outputs = (edge.source.node == NodeFactory<Graph>::GRAPH_ID)
                    ? graph_outputs
                    : nodes[edge.source.node]->outputs();

                auto& out = outputs[edge.source.port];
                auto& in = inputs[edge.destination.port];
                jassert(!seen_inputs.contains(&in));
                seen_inputs.emplace(&in);
                out.fan.emplace_back(&in);
            }

            return std::make_unique<Graph>(
                std::move(nodes),
                std::move(graph_inputs),
                std::move(graph_outputs)
            );
        }

        template<class T>
        auto add_node() {
            auto node_ptr = std::make_unique<NodeFactory<T>>();
            auto node_ref = node_ptr.get();
            factories.emplace_back(std::move(node_ptr));
            return std::make_tuple(node_ref, factories.size() - 1);
        }

        size_t add_input_port() noexcept {
            return num_inputs++;
        }

        size_t add_output_port() noexcept {
            return num_outputs++;
        }

        void connect(PortId source, PortId destination) {
            edges.emplace_back(source, destination);
        }
    };
}

#if JUCE_DEBUG

static int test1 = []() -> int {
    iv::InputPort in;
    iv::OutputPort out;

    out.fan.emplace_back(&in);
    in.add_latency(out.latency());

    iv::Sample expected = 3.0;
    out.push(expected);
    auto actual = in.next();
    jassert(actual == expected);
    return 0;
}();

static int test2 = []() -> int {
    iv::InputPort in;
    iv::OutputPort out{1};

    out.fan.emplace_back(&in);
    in.add_latency(out.latency());

    iv::Sample expected = 3.0;
    out.push(expected);
    in.next();
    auto actual = in.next();
    jassert(actual == expected);
    return 0;
}();

static int test3 = []() -> int {
    iv::InputPort in;
    iv::OutputPort out{1};

    out.fan.emplace_back(&in);
    in.add_latency(out.latency());

    iv::Sample expected = 3.0;
    out.push(expected);
    auto actual = out.back();
    jassert(actual == expected);
    return 0;
}();

static int test_integration1 = []() -> int {
    iv::NodeFactory<iv::Graph> factory;
    auto [sum, sum_id] = factory.add_node<iv::SumNode>();
    auto p1 = sum->add_input_port();
    auto p2 = sum->add_input_port();
    auto in1 = factory.add_input_port();
    auto out1 = factory.add_output_port();
    auto out2 = factory.add_output_port();

    factory.connect({ iv::NodeFactory<iv::Graph>::GRAPH_ID, out1 }, { sum_id, p1 });
    factory.connect({ iv::NodeFactory<iv::Graph>::GRAPH_ID, out2 }, { sum_id, p2 });
    factory.connect({ sum_id, 0 }, { iv::NodeFactory<iv::Graph>::GRAPH_ID, in1 });

    auto graph = factory.create();
    graph->outputs()[0].update(0, 1.5);
    graph->outputs()[1].update(0, 2.5);
    graph->tick({});
    auto actual = graph->inputs()[0].next();

    jassert(actual == 4);
    return 0;
}();

#endif
