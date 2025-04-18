#pragma once
#include <vector>
#include <array>
#include <memory>
#include <span>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <deque>
#include <cassert>
#include <random>

enum struct PolyblepSide {
    LEFT,
    RIGHT,
};

static float polyblep_phi(float const& sample, float const& warp_threshold) {
    auto const res = (sample + warp_threshold) / 2.f;
    return res;
}

static float polyblep_p(float const& phi, float const& delta, float const& warp_threshold, PolyblepSide side) {
    if (side == PolyblepSide::RIGHT && phi < delta) {
        auto const& first_order = 2.f * phi / delta;
        auto const& second_order = phi / delta;
        return (first_order - second_order * second_order - 1) * warp_threshold;
    }
    if (side == PolyblepSide::LEFT && delta > warp_threshold - phi) {
        auto const& second_order = (phi - warp_threshold) / delta + 1;
        return second_order * second_order * warp_threshold;
    }
    return 0;
}

static float polyblep_error(float sample, float delta, float warp_threshold, PolyblepSide side) {
    float sign = 1.f - std::signbit(delta) * 2.f;
    delta = std::abs(delta);

    float const phi = polyblep_phi(sample, warp_threshold);
    float const p = polyblep_p(phi, delta, warp_threshold, side) * sign;
    return p;
}

static inline float warp_pm1(float x, float limit) noexcept {
    const float period = 2 * limit;
    return x - std::floor((x + limit) / period) * period;
}

namespace iv {
    using Sample = float;

    enum struct MidiMessageType {
        NONE,
        NOTE_ON,
        NOTE_OFF,
    };

    struct MidiMessage {
        MidiMessageType type = MidiMessageType::NONE;
        union {
            struct {
                uint8_t note_number;
                uint8_t amplitude;
            } note_on;
            struct {
                uint8_t note_number;
            } note_off;
        };
    };

    /* ─────────────  Ports  ───────────── */
    struct InputPort {
        Sample _default;
        size_t _history;
        size_t _latency;
        std::vector<Sample> _buffer;
        size_t _index;

        explicit InputPort(size_t history = 0, Sample default_value = 0.0) noexcept :
            _default(default_value),
            _history(history),
            _latency(0),
            _index(0)
        {
            add_latency(0);  // init buffer
        }

        ~InputPort() = default;

        explicit InputPort(InputPort const&) = delete;
        explicit InputPort(InputPort&& other) noexcept {
            *this = std::move(other);
        }

        InputPort& operator=(InputPort const&) = delete;
        InputPort& operator=(InputPort&& other) noexcept {
            _buffer = std::move(other._buffer);
            _index = other._index;
            _default = other._default;
            _history = other._history;
            _latency = other._latency;
            return *this;
        }

        Sample get(size_t offset = 0) noexcept {
            if (offset > _history) return 0.0;
            size_t idx = (_index + _buffer.size() - offset) & (_buffer.size() - 1);  // buffer size is a power of 2
            return _buffer[idx];
        }

        void push(Sample value) noexcept {
            _index = (_index + 1) & (_buffer.size() - 1);  // buffer size is a power of 2
            update(value);
        }

        void update(Sample value, size_t offset = 0) noexcept {
            if (offset > _latency) return;
            size_t idx = (_index + _buffer.size() + _latency - offset) & (_buffer.size() - 1);  // buffer size is a power of 2
            _buffer[idx] = value;
        }

        void add_latency(size_t latency) noexcept {
            _latency += latency;
            size_t min_buffer_size = _latency + _history + 1;
            _buffer.assign(size_t(1) << size_t(std::ceil(std::log2(min_buffer_size))), _default);
            _buffer.shrink_to_fit();
        }

        size_t get_latency() const noexcept {
            return _latency;
        }
    };

    struct OutputPort {
        size_t _latency;
        std::vector<InputPort*> fan;
        std::vector<Sample> _buffer;
        size_t _index;

        explicit OutputPort(size_t latency = 0) noexcept :
            _latency(latency),
            _buffer(size_t(1) << size_t(std::ceil(std::log2(latency + 1)))),
            _index(0)
        {}

        void connect_to(InputPort* in) {
            assert(in->get_latency() == 0);
            fan.push_back(in);
            in->add_latency(_latency);
        }

        void push(Sample value) noexcept {
            _index = (_index + 1) & (_buffer.size() - 1);  // buffer size is a power of 2
            size_t idx = (_index + _buffer.size() + _latency) & (_buffer.size() - 1);  // buffer size is a power of 2
            _buffer[idx] = value;
            for (auto* dst : fan) {
                dst->push(value);
            }
        }

        void update(Sample value, size_t offset = 0) noexcept {
            if (offset > _latency) return;
            size_t idx = (_index + _buffer.size() + _latency - offset) & (_buffer.size() - 1);  // buffer size is a power of 2
            _buffer[idx] = value;
            for (auto* dst : fan) {
                dst->update(value, offset);
            }
        }

        Sample get(size_t offset = 0) const noexcept {
            if (offset > _latency) return 0.0;
            size_t idx = (_index + _buffer.size() + offset) & (_buffer.size() - 1);  // buffer size is a power of 2
            return _buffer[idx];
        }

        size_t get_latency() const noexcept {
            return _latency;
        }
    };

    /* ─────────────  Node base  ───────────── */
    struct Node {
        virtual ~Node() = default;
        virtual void tick(std::span<MidiMessage const> const& midi) noexcept = 0;
        virtual size_t inner_latency() const noexcept {
            return 0;
        }

        std::span<InputPort> inputs() noexcept {
            auto span = const_cast<Node const*>(this)->inputs_impl();
            return { const_cast<InputPort*>(span.data()), span.size(), };
        }

        std::span<InputPort const> inputs() const noexcept {
            return inputs_impl();
        }

        std::span<OutputPort> outputs() noexcept {
            auto span = const_cast<Node const*>(this)->outputs_impl();
            return { const_cast<OutputPort*>(span.data()), span.size(), };
        }

        std::span<OutputPort const> outputs() const noexcept {
            return outputs_impl();
        }

    protected:
        virtual std::span<InputPort const> inputs_impl() const noexcept {
            return {};
        };
        virtual std::span<OutputPort const> outputs_impl() const noexcept {
            return {};
        }
    };

    /* ─────────────  Concrete nodes  ───────────── */
    class SumNode : public Node {
        std::vector<InputPort> _ins;
        OutputPort _out;

    public:
        explicit SumNode(std::vector<InputPort>&& ins) noexcept: _ins(std::move(ins)) {}

        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            Sample result = 0.0;
            for (auto& in : _ins) {
                result += in.get();
            }
            _out.push(result);
        }
        std::span<InputPort const> inputs_impl() const noexcept override { return _ins; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    };

    class MultiplyNode : public Node {
        std::vector<InputPort> _ins;
        OutputPort _out;

    public:
        explicit MultiplyNode(std::vector<InputPort>&& ins) noexcept : _ins(std::move(ins)) {}

        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            Sample result = 1.0;
            for (auto& in : _ins) {
                result *= in.get();
            }
            _out.push(result);
        }
        std::span<InputPort const> inputs_impl() const noexcept override { return _ins; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    };

    class IntegratorNode : public Node {
        InputPort _in_velocity, _in_previous, _in_sample_rate{0, 44100};
        OutputPort _out;

    public:
        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            _out.push(_in_velocity.get() / _in_sample_rate.get() + _in_previous.get());
        }
        std::span<InputPort const> inputs_impl() const noexcept override { return { &_in_velocity, 3 }; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    };

    struct WarperNode : public Node {
        InputPort _in, _in_threshold;
        OutputPort _out{ 1 }, _out_aliased;

        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            Sample threshold = _in_threshold.get();
            Sample sample_prev = _out_aliased.get();
            Sample sample = _in.get();
            Sample sample_warped = sample;
            bool warped = false;

            if (sample > threshold) { sample_warped = warp_pm1(sample, threshold); warped = true; }
            else if (sample < -threshold) { sample_warped = warp_pm1(sample, threshold); warped = true; }
            _out_aliased.push(sample_warped);

            Sample sample_warped_aa = sample_warped;
            if (warped) {
                Sample delta = (sample - sample_prev) / 2.0;
                _out.update(sample_prev - polyblep_error(sample_prev, delta, threshold, PolyblepSide::LEFT));
                sample_warped_aa -= polyblep_error(sample_warped_aa, delta, threshold, PolyblepSide::RIGHT);
            }
            _out.push(sample_warped_aa);
        }
        std::span<InputPort const> inputs_impl() const noexcept override { return { &_in, 2 }; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 2 }; }
    };

    struct IirFilterNode : public Node {
        InputPort _in{4}, _inw[4], _outw[3];
        OutputPort _out{3};

        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            Sample weighted_sum = 0.0;
            for (size_t i = 0; i < sizeof(_inw) / sizeof(InputPort); ++i) {
                weighted_sum += _in.get(i) * _inw[i].get();
            }
            for (size_t i = 0; i < sizeof(_outw) / sizeof(InputPort); ++i) {
                weighted_sum += _out.get(i) * _outw[i].get();
            }
            _out.push(weighted_sum);
        }
        std::span<InputPort const> inputs_impl() const noexcept override { return { &_in, 1 + (sizeof(_inw) + sizeof(_outw)) / sizeof(InputPort)}; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    };

    class ConstantNode : public Node {
        OutputPort _output;
        Sample _value;

    public:
        explicit ConstantNode(Sample value) noexcept : _value(value) {}

        void tick(std::span<MidiMessage const> const&) noexcept override {
            _output.push(_value);
        }

        std::span<OutputPort const> outputs_impl() const noexcept override { return { &_output, 1 }; }
    };

    class UniformNoiseNode : public Node {
        std::mt19937 _generator{std::random_device{}()};
        std::uniform_real_distribution<Sample> _distribution{-1.0f,1.0f};
        OutputPort _output;

        void tick(std::span<MidiMessage const> const&) noexcept override {
            _output.push(_distribution(_generator));
        }

        std::span<OutputPort const> outputs_impl() const noexcept override { return { &_output, 1 }; }
    };

    /* ─────────────  Patch  ───────────── */
    class Graph : public Node {
        std::vector<std::unique_ptr<Node>> _nodes;
        std::vector<InputPort> _private_ins;
        std::vector<OutputPort> _private_outs;
        std::vector<InputPort> _public_ins;
        std::vector<OutputPort> _public_outs;

    public:
        explicit Graph(
            std::vector<std::unique_ptr<Node>>&& nodes,
            std::vector<InputPort>&& private_ins,
            std::vector<OutputPort>&& private_outs
        ) noexcept:
            _nodes(std::move(nodes)),
            _private_ins(std::move(private_ins)),
            _private_outs(std::move(private_outs)),
            _public_ins(_private_outs.size()),
            _public_outs(_private_ins.size())
        {
            pseudo_topological_sort();
            init_buffers();
        }

    private:
        void pseudo_topological_sort() {
            const size_t n = _nodes.size();

            std::unordered_map<InputPort*, size_t> owner_of;
            for (size_t i = 0; i < n; ++i) {
                for (auto& in : _nodes[i]->inputs()) {
                    owner_of[&in] = i;
                }
            }

            std::unordered_map<InputPort*, size_t> parent_of;
            for (size_t i = 0; i < n; ++i) {
                for (auto& out : _nodes[i]->outputs()) {
                    for (auto const dst : out.fan) {
                        parent_of[dst] = i;
                    }
                }
            }

            auto make_heads_queue = [&]() {
                std::deque<size_t> queue;
                for (size_t i = 0; i < n; ++i) {
                    bool all_inputs_disconnected = true;
                    for (auto& in : _nodes[i]->inputs()) {
                        if (parent_of.contains(&in)) {
                            all_inputs_disconnected = false;
                            break;
                        }
                    }
                    if (!all_inputs_disconnected) continue;
                    queue.push_back(i);
                }
                for (auto& out : _private_outs) {
                    for (auto& dst : out.fan) {
                        if (!owner_of.contains(dst)) continue;
                        size_t child = owner_of[dst];
                        queue.push_back(child);
                    }
                }
                return queue;
            };

            std::vector<std::unordered_set<size_t>> cyclic_parents_of(n);
            {
                auto queue = make_heads_queue();
                std::vector<bool> seen(n, false);
                while (!queue.empty()) {
                    size_t i = queue.front();
                    queue.pop_front();
                    if (seen[i]) continue;
                    seen[i] = true;

                    std::vector<bool> inner_seen(n, false);
                    std::deque<size_t> inner_queue;
                    inner_queue.push_back(i);

                    while (!inner_queue.empty()) {
                        size_t node = inner_queue.front();
                        inner_queue.pop_front();
                        if (inner_seen[node]) continue;
                        inner_seen[node] = true;

                        for (auto& out : _nodes[node]->outputs()) {
                            for (auto& dst : out.fan) {
                                if (!owner_of.contains(dst)) continue;
                                size_t child = owner_of[dst];
                                if (!cyclic_parents_of[node].empty()) continue;
                                if (child == i) {
                                    cyclic_parents_of[i].insert(node);
                                }
                                inner_queue.push_back(child);
                            }
                        }
                    }

                    for (auto& out : _nodes[i]->outputs()) {
                        for (auto& dst : out.fan) {
                            if (!owner_of.contains(dst)) continue;
                            size_t child = owner_of[dst];
                            queue.push_back(child);
                        }
                    }
                }
            }

            auto queue = make_heads_queue();
            std::vector<bool> placed(n, false);
            std::vector<size_t> sorted;
            sorted.reserve(n);

            while (!queue.empty()) {
                size_t node = queue.front();
                queue.pop_front();
                if (placed[node]) continue;

                bool all_dependencies_satisfied = true;
                for (auto& in : _nodes[node]->inputs()) {
                    if (!parent_of.contains(&in)) continue;
                    size_t parent = parent_of[&in];
                    if (!placed[parent] && !cyclic_parents_of[node].contains(parent)) {
                        all_dependencies_satisfied = false;
                        break;
                    }
                }
                
                if (all_dependencies_satisfied) {
                    for (auto& out : _nodes[node]->outputs()) {
                        for (auto& dst : out.fan) {
                            if (!owner_of.contains(dst)) continue;
                            size_t child = owner_of[dst];
                            queue.push_back(child);
                        }
                    }
                    sorted.push_back(node);
                    placed[node] = true;
                }
                else {
                    queue.push_back(node);
                }
            }
            
            std::vector<std::unique_ptr<Node>> sorted_nodes;
            sorted_nodes.reserve(n);
            for (size_t i : sorted) {
                sorted_nodes.push_back(std::move(_nodes[i]));
            }
            _nodes.swap(sorted_nodes);
        }

        void init_buffers() {
            std::unordered_map<InputPort const*, size_t> input_global_latencies;

            for (size_t node_idx = 0; node_idx < _nodes.size() + 1; ++node_idx) {
                // node_idx == _nodes.size() means this graph
                auto node = (node_idx < _nodes.size())
                    ? _nodes[node_idx].get()
                    : this;
                auto node_inputs = (node_idx < _nodes.size())
                    ? _nodes[node_idx]->inputs()
                    : _private_ins;

                size_t node_global_latency = 0;
                for (auto& in : node_inputs) {
                    node_global_latency = std::max(node_global_latency, input_global_latencies[&in]);
                }
                for (auto& in : node_inputs) {
                    in.add_latency(node_global_latency - input_global_latencies[&in]);
                }
                if (node == this) continue;
                node_global_latency += node->inner_latency();
                for (auto& out : node->outputs()) {
                    for (auto& dst : out.fan) {
                        input_global_latencies[dst] = node_global_latency + out.get_latency();
                    }
                }
            }
        }

    public:
        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            for (size_t i = 0; i < _private_outs.size(); ++i) {
                _private_outs[i].push(_public_ins[i].get());
            }
            for (auto& node : _nodes) {
                node->tick(midi);
            }
            for (size_t i = 0; i < _private_ins.size(); ++i) {
                _public_outs[i].push(_private_ins[i].get());
            }
        }

        size_t inner_latency() const noexcept override {
            std::unordered_map<InputPort const*, size_t> input_global_latencies;
            size_t max_latency = 0;

            for (size_t node_idx = 0; node_idx < _nodes.size() + 1; ++node_idx) {
                // node_idx == _nodes.size() means this graph
                auto node = (node_idx < _nodes.size())
                    ? _nodes[node_idx].get()
                    : this;
                auto node_inputs = (node_idx < _nodes.size())
                    ? const_cast<Node const*>(_nodes[node_idx].get())->inputs()
                    : _private_ins;

                size_t node_global_latency = 0;
                for (auto& in : node_inputs) {
                    node_global_latency = std::max(node_global_latency, input_global_latencies[&in]);
                }
                if (node == this) continue;
                node_global_latency += node->inner_latency();
                for (auto& out : node->outputs()) {
                    for (auto& dst : out.fan) {
                        size_t new_latency = node_global_latency + out.get_latency();
                        max_latency = std::max(max_latency, new_latency);
                        input_global_latencies[dst] = new_latency;
                    }
                }
            }

            return max_latency;
        }

        std::span<InputPort const> inputs_impl() const noexcept override { return _public_ins; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return _public_outs; }
    };

    struct NodeFactoryBase {
        virtual ~NodeFactoryBase() = default;
        virtual std::unique_ptr<Node> create() const = 0;
        virtual std::unique_ptr<NodeFactoryBase> clone() const = 0;
    };

    template<class T>
    struct NodeFactory;

    template<class T>
    struct NodeFactoryBaseTemplate : public NodeFactoryBase {
        std::unique_ptr<NodeFactoryBase> clone() const override {
            return std::make_unique<NodeFactory<T>>();
        }
        std::unique_ptr<Node> create() const override {
            return std::unique_ptr<Node>(create_t().release());
        }
        virtual std::unique_ptr<T> create_t() const = 0;
    };

    template<class T>
    struct NodeFactory : public NodeFactoryBaseTemplate<T> {
        std::unique_ptr<T> create_t() const override {
            return std::make_unique<T>();
        }
    };

    template<>
    struct NodeFactory<SumNode> : public NodeFactoryBaseTemplate<SumNode> {
        size_t _num_inputs;

        explicit NodeFactory(size_t num_inputs = 0) noexcept : _num_inputs(num_inputs) {}

        std::unique_ptr<SumNode> create_t() const override {
            std::vector<InputPort> inputs(_num_inputs);
            return std::make_unique<SumNode>(std::move(inputs));
        }

        std::unique_ptr<NodeFactoryBase> clone() const override {
            return std::make_unique<NodeFactory>(_num_inputs);
        }

        size_t add_input_port() noexcept {
            return _num_inputs++;
        }
    };

    template<>
    struct NodeFactory<MultiplyNode> : public NodeFactoryBaseTemplate<MultiplyNode> {
        size_t _num_inputs;

        explicit NodeFactory(size_t num_inputs = 0) noexcept : _num_inputs(num_inputs) {}

        std::unique_ptr<MultiplyNode> create_t() const override {
            std::vector<InputPort> inputs(_num_inputs);
            return std::make_unique<MultiplyNode>(std::move(inputs));
        }

        std::unique_ptr<NodeFactoryBase> clone() const override {
            return std::make_unique<NodeFactory>(_num_inputs);
        }

        size_t add_input_port() noexcept {
            return _num_inputs++;
        }
    };

    template<>
    class NodeFactory<ConstantNode> : public NodeFactoryBaseTemplate<ConstantNode> {
        Sample _value;

    public:
        explicit NodeFactory(Sample value = 0) noexcept : _value(value) {}

        std::unique_ptr<ConstantNode> create_t() const {
            return std::make_unique<ConstantNode>(_value);
        }

        std::unique_ptr<NodeFactoryBase> clone() const override {
            return std::make_unique<NodeFactory>(_value);
        }

        void set_value(Sample value) {
            _value = value;
        }
    };

    using PortId = size_t;
    struct NodePortId {
        size_t node;
        PortId port;
    };

    template<>
    struct NodeFactory<Graph> : public NodeFactoryBaseTemplate<Graph> {
        static const size_t GRAPH_ID = std::numeric_limits<size_t>::max();

        struct Edge { NodePortId source, destination; };

        std::vector<std::unique_ptr<NodeFactoryBase>> factories;
        std::vector<Edge> edges;
        size_t num_inputs{0}, num_outputs{0};

        explicit NodeFactory() noexcept {}
        explicit NodeFactory(NodeFactory const& other) {
            *this = other;
        }
        explicit NodeFactory(NodeFactory&& other) noexcept
        {
            *this = std::move(other);
        }

        NodeFactory& operator=(NodeFactory const& other) {
            for (auto const& factory : other.factories) {
                factories.emplace_back(factory->clone());
            }
            edges = other.edges;
            num_inputs = other.num_inputs;
            num_outputs = other.num_outputs;
            return *this;
        }

        NodeFactory& operator=(NodeFactory&& other) noexcept {
            factories = std::move(other.factories);
            edges = std::move(other.edges);
            num_inputs = other.num_inputs;
            num_outputs = other.num_outputs;
            other.num_inputs = 0;
            other.num_outputs = 0;
            return *this;
        }

        std::unique_ptr<Graph> create_t() const override {
            std::vector<std::unique_ptr<Node>> nodes;
            std::unordered_map<InputPort*, std::vector<OutputPort*>> port_mappings;

            // note: number of private inputs corresponds to number of public outputs
            // note: number of private outputs corresponds to number of public inputs
            std::vector<InputPort> graph_private_inputs(num_outputs);
            std::vector<OutputPort> graph_private_outputs(num_inputs);

            for (auto const& factory : factories) {
                nodes.emplace_back(factory->create());
            }

            for (auto const& edge : edges) {
                std::span<InputPort> inputs = (edge.destination.node == NodeFactory<Graph>::GRAPH_ID)
                    ? graph_private_inputs
                    : nodes[edge.destination.node]->inputs();
                std::span<OutputPort> outputs = (edge.source.node == NodeFactory<Graph>::GRAPH_ID)
                    ? graph_private_outputs
                    : nodes[edge.source.node]->outputs();

                auto const out = &outputs[edge.source.port];
                auto const in = &inputs[edge.destination.port];
                port_mappings[in].push_back(out);
            }

            for (auto const& [in, outs] : port_mappings) {
                if (outs.size() == 1) {
                    outs[0]->connect_to(in);
                }
                else {
                    auto sum_node = NodeFactory<SumNode>(outs.size()).create();
                    for (size_t i = 0; i < outs.size(); ++i) {
                        outs[i]->connect_to(&sum_node->inputs()[i]);
                    }
                    sum_node->outputs()[0].connect_to(in);
                    nodes.emplace_back(std::move(sum_node));
                }
            }

            return std::make_unique<Graph>(
                std::move(nodes),
                std::move(graph_private_inputs),
                std::move(graph_private_outputs)
            );
        }

        std::unique_ptr<NodeFactoryBase> clone() const override {
            auto graph = std::make_unique<NodeFactory<Graph>>();
            *graph = *this;
            return graph;
        }

        template<class T, class... Args>
        auto add_node(Args&&... args) {
            auto node_ptr = std::make_unique<NodeFactory<T>>(std::forward<Args>(args)...);
            auto node_ref = node_ptr.get();
            factories.emplace_back(std::move(node_ptr));
            return std::make_tuple(node_ref, factories.size() - 1);
        }

        size_t duplicate_node(size_t node_id) {
            auto const& factory = factories[node_id];
            factories.emplace_back(factory->clone());
            size_t new_node_id = factories.size() - 1;
            size_t edges_size = edges.size();
            for (size_t i = 0; i < edges_size; ++i) {
                Edge edge_clone = edges[i];
                bool needs_duplicate = false;
                if (edge_clone.destination.node == node_id) {
                    edge_clone.destination.node = new_node_id;
                    needs_duplicate = true;
                }
                if (edge_clone.source.node == node_id) {
                    edge_clone.source.node = new_node_id;
                    needs_duplicate = true;
                }
                if (needs_duplicate) {
                    edges.push_back(edge_clone);
                }
            }
            return new_node_id;
        }

        PortId add_input_port() noexcept {
            return num_inputs++;
        }

        PortId add_output_port() noexcept {
            return num_outputs++;
        }

        void connect(NodePortId source, NodePortId destination) {
            edges.emplace_back(source, destination);
        }
    };

    //class RemoteSinkNode : public Node {
    //    InputPort _in;
    //    std::vector<OutputPort> _remote_outs;

    //public:
    //    explicit RemoteSinkNode(std::vector<OutputPort>&& remote_outs) noexcept :
    //        _remote_outs(std::move(remote_outs))
    //    {}

    //    void tick(std::span<MidiMessage const> const&) noexcept override {
    //        for (auto& remote_out : _remote_outs) {
    //            remote_out.push(_in.get());
    //        }
    //    }

    //    std::span<InputPort const> inputs_impl() const noexcept override { return { &_in, 1 }; }
    //    std::span<OutputPort const> outputs_impl() const noexcept override { return _remote_outs; }
    //};

    //template<>
    //class NodeFactory<RemoteSinkNode> : public NodeFactoryBaseTemplate<RemoteSinkNode> {
    //    size_t _num_remotes;

    //public:
    //    template<class... Args>
    //    explicit NodeFactory() noexcept :
    //        _num_remotes(0)
    //    {}

    //    std::unique_ptr<RemoteSinkNode> create_t() const override {
    //        std::vector<OutputPort> remote_outs(_num_remotes);
    //        return std::make_unique<RemoteSinkNode>(std::move(remote_outs));
    //    }

    //    std::unique_ptr<NodeFactoryBase> clone() const override {
    //        assert(false); // cannot clone this, sorry liskov
    //    }

    //    PortId add_remote() noexcept {
    //        return _num_remotes++;
    //    }
    //};
    //
    //class RemoteSourceNode : public Node {
    //    InputPort _remote_in;
    //    OutputPort _out;

    //public:
    //    void tick(std::span<MidiMessage const> const&) noexcept override {}

    //    std::span<InputPort const> inputs_impl() const noexcept override { return { &_remote_in, 1 }; }
    //    std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    //};

    //template<>
    //class NodeFactory<RemoteSourceNode> : public NodeFactoryBaseTemplate<RemoteSourceNode> {
    //    NodeFactory<RemoteSinkNode>* _connection;
    //    PortId _port_id;

    //public:
    //    explicit NodeFactory(NodeFactory<RemoteSinkNode>* connection) noexcept :
    //        _connection(connection),
    //        _port_id(connection->add_remote())
    //    {
    //        _port_id = connection->add_remote();
    //    }

    //    std::unique_ptr<RemoteSourceNode> create_t() const override {
    //        return std::make_unique<RemoteSourceNode>(_connection->get_out_port());
    //    }

    //    std::unique_ptr<NodeFactoryBase> clone() const override {
    //        return std::make_unique<NodeFactory>(_connection);
    //    }
    //};

    struct MidiNode : public Node {
        static constexpr size_t const MAX_MIDI_NOTES = 128;
        static constexpr size_t const BASE_INPUTS = 1;
        static constexpr size_t const BASE_GRAPH_INPUTS = 2;

        struct MidiNoteState {
            size_t ttl{0};
            size_t amplitude{0};
        };

        std::array<std::unique_ptr<Node>, MAX_MIDI_NOTES> _graphs;
        std::array<MidiNoteState, MAX_MIDI_NOTES> _note_states;
        std::vector<uint8_t> _active_voices;
        size_t _max_ttl;
        std::vector<InputPort> _public_inputs;
        OutputPort out_mix;

        explicit MidiNode(
            NodeFactory<Graph> const& voice_factory,
            std::vector<InputPort>&& extra_public_inputs
        ) noexcept {
            _public_inputs.reserve(extra_public_inputs.size() + BASE_INPUTS);
            _public_inputs.emplace_back(std::pow(10.0, -60.0 / 20.0)); // silence_threshold, -60db
            assert(_public_inputs.size() == BASE_INPUTS);
            for (auto& extra_input : extra_public_inputs) {
                _public_inputs.emplace_back(std::move(extra_input));
            }
            assert(_public_inputs.size() == _public_inputs.capacity());

            for (size_t i = 0; i < MAX_MIDI_NOTES; ++i) {
                _graphs[i] = voice_factory.create();
            }
            _max_ttl = dynamic_cast<Graph*>(_graphs[0].get())->inner_latency();
            _active_voices.reserve(MAX_MIDI_NOTES);
        }

        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            auto silence_threshold = _public_inputs[0].get();

            for (auto const& midi_message : midi) {
                if (midi_message.type == MidiMessageType::NOTE_ON) {
                    auto& note_state = _note_states[midi_message.note_on.note_number];
                    note_state.amplitude = midi_message.note_on.amplitude;
                    note_state.ttl = _max_ttl;
                    _active_voices.push_back(midi_message.note_on.note_number);
                }
                if (midi_message.type == MidiMessageType::NOTE_OFF) {
                    auto& note_state = _note_states[midi_message.note_off.note_number];
                    note_state.amplitude = 0;
                }
            }

            Sample result = 0.0;
            for (size_t i = 0; i < _active_voices.size(); ++i) {
                uint8_t note_number = _active_voices[i];
                auto& note_state = _note_states[note_number];
                auto& graph = _graphs[note_number];
                if (note_state.amplitude) {
                    note_state.ttl = _max_ttl;
                }
                else if (note_state.ttl) {
                    --note_state.ttl;
                }
                else if (auto last_output = graph->outputs()[0].get(); last_output <= silence_threshold && last_output >= -silence_threshold) {
                    _active_voices.erase(_active_voices.begin() + i);
                    --i; // underflows then overflows for i=0 but should be fine
                    continue;
                }

                auto const graph_inputs = graph->inputs();
                graph_inputs[0].push(note_number_to_frequency(note_number));
                graph_inputs[1].push(note_state.amplitude / 127.0);
                auto extra_inputs = get_extra_inputs();
                for (size_t extra_i = 0; extra_i < extra_inputs.size(); ++extra_i) {
                    graph_inputs[BASE_GRAPH_INPUTS+extra_i].push(extra_inputs[extra_i].get());
                }
                graph->tick(midi);
                result += graph->outputs()[0].get();
            }
            out_mix.push(result);
        }

        size_t inner_latency() const noexcept override {
            return _max_ttl;
        }

        static Sample note_number_to_frequency(uint8_t note_number) {
            return Sample(440.0 * std::pow(2.0, (note_number - 69) / 12.0));
        }

        std::span<InputPort const> inputs_impl() const noexcept override { return _public_inputs; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return { &out_mix, 1 }; }

        std::span<InputPort> get_extra_inputs() {
            return { _public_inputs.data() + BASE_INPUTS, _public_inputs.size() - BASE_INPUTS };
        }
    };

    template<>
    class NodeFactory<MidiNode> : public NodeFactoryBaseTemplate<MidiNode> {
        NodeFactory<Graph> _voice_factory;
        PortId _frequency_port, _amplitude_port;
        PortId _output_port;
        size_t _num_extra_public_inputs;

    public:
        explicit NodeFactory() noexcept:
            _frequency_port(_voice_factory.add_input_port()),
            _amplitude_port(_voice_factory.add_input_port()),
            _output_port(_voice_factory.add_output_port()),
            _num_extra_public_inputs(0)
        {}

        std::unique_ptr<MidiNode> create_t() const override {
            std::vector<InputPort> extra_public_inputs(_num_extra_public_inputs);
            return std::make_unique<MidiNode>(_voice_factory, std::move(extra_public_inputs));
        }

        std::unique_ptr<NodeFactoryBase> clone() const override {
            auto factory = std::make_unique<NodeFactory>();
            factory->_voice_factory = _voice_factory;
            factory->_frequency_port = _frequency_port;
            factory->_amplitude_port = _amplitude_port;
            factory->_output_port = _output_port;
            factory->_num_extra_public_inputs = _num_extra_public_inputs;
            return factory;
        }

        NodeFactory<Graph>& get_voice_factory() {
            return _voice_factory;
        }

        PortId get_voice_frequency_port() const noexcept {
            return _frequency_port;
        }

        PortId get_voice_amplitude_port() const noexcept {
            return _amplitude_port;
        }

        PortId get_voice_output_port() const noexcept {
            return _output_port;
        }

        // returns [midi port id, voice port id]
        std::tuple<PortId, PortId> add_forwarding_input_port() noexcept {
            PortId midi_id = _num_extra_public_inputs++;
            PortId voice_id = _voice_factory.add_input_port();
            return std::make_tuple(midi_id + MidiNode::BASE_INPUTS, voice_id);
        }
    };
}
