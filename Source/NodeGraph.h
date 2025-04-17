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
    auto const res = (sample / warp_threshold + 1.f) / 2.f;
    if (!std::isfinite(res)) return 0.5;
    return res;
}

static float polyblep_p(float const& phi, float const& delta, PolyblepSide side) {
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

static float polyblep_error(float sample, float delta, float warp_threshold, PolyblepSide side) {
    float sign = 1.f - std::signbit(delta) * 2.f;
    delta = std::abs(delta) / warp_threshold;

    float const phi = polyblep_phi(sample, warp_threshold);
    if (!std::isfinite(phi)) return 0.f;
    float const p = polyblep_p(phi, delta, side) * warp_threshold * sign;
    if (!std::isfinite(p)) return 0.f;
    return p;
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
                uint8_t amplitude;
            } note_off;
        };
    };

    /* ─────────────  Ports  ───────────── */
    struct InputPort {
        std::vector<Sample> _buffer;
        size_t _write_idx{0};
        Sample _default;

        explicit InputPort(Sample default_value = 0.0) noexcept: _default(default_value) {
            _buffer.push_back(default_value);
        }

        explicit InputPort(InputPort const&) = delete;
        explicit InputPort(InputPort&& other) noexcept {
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

        void update(Sample value, size_t offset = 0) noexcept {
            // update the current position in the buffer
            // offset brings the write head closer to a real time update
            size_t idx = (_write_idx + _buffer.size() - offset) % _buffer.size();
            _buffer[idx] = value;
        }

        void add_latency(size_t latency) noexcept {
            _buffer.assign(_buffer.size() + latency, _default);
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

        explicit OutputPort(size_t latency = 0) noexcept: _latency(latency), _buffer(latency + 1), _index(0) {}

        void connect_to(InputPort* in) {
            assert(in->get_latency() == 0);
            fan.push_back(in);
            in->add_latency(_latency);
        }

        void push(Sample v) noexcept {
            next();
            update(v);
        }

        void update(Sample value, size_t offset = 0) noexcept {
            if (offset > _latency) return;
            size_t idx = (_index + _buffer.size() - offset) % _buffer.size();
            _buffer[idx] = value;
            for (auto* dst : fan) {
                dst->update(value, offset);
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

    /* ─────────────  Helpers  ───────────── */
    static inline Sample warp_pm1(Sample x, Sample threshold) {
        if (x > threshold)  return std::fmod(x + threshold, 2 * threshold) - threshold;
        if (x < -threshold) return -std::fmod(-x + threshold, 2 * threshold) + threshold;
        return x;
    }

    /* ─────────────  Concrete nodes  ───────────── */
    class SumNode : public Node {
        std::vector<InputPort> _ins;
        OutputPort _out;

    public:
        explicit SumNode(std::vector<InputPort>&& ins) noexcept: _ins(std::move(ins)) {}

        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            Sample result = 0.0;
            for (auto& in : _ins) {
                result += in.next();
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
                result *= in.next();
            }
            _out.push(result);
        }
        std::span<InputPort const> inputs_impl() const noexcept override { return _ins; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    };

    class IntegratorNode : public Node {
        InputPort _in_velocity, _in_previous, _in_sample_rate{44100};
        OutputPort _out;

    public:
        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            _out.push(_in_velocity.next() / _in_sample_rate.next() + _in_previous.next());
        }
        std::span<InputPort const> inputs_impl() const noexcept override { return { &_in_velocity, 3 }; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    };

    struct WarperNode : public Node {
        InputPort _in, _in_threshold;
        OutputPort _out{1}, _out_aliased;

        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            Sample threshold = _in_threshold.next();
            Sample sample_prev = _out_aliased.back();
            Sample sample = _in.next();
            Sample sample_warped = sample;
            bool warped = false;
            
            if (sample > threshold) { sample_warped = warp_pm1(sample, threshold); warped = true; }
            else if (sample < -threshold) { sample_warped = warp_pm1(sample, threshold); warped = true; }
            _out_aliased.push(sample_warped);

            Sample sample_warped_aa = sample_warped;
            if (warped) {
                Sample delta = (sample - sample_prev) / 2.0;
                sample_warped_aa -= polyblep_error(sample_warped_aa, delta, threshold, PolyblepSide::RIGHT);
                _out.update(sample_prev - polyblep_error(sample_prev, delta, threshold, PolyblepSide::LEFT));
            }
            _out.push(sample_warped_aa);
        }
        std::span<InputPort const> inputs_impl() const noexcept override { return { &_in, 2 }; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 2 }; }
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
        OutputPort _output;

        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            std::mt19937 generator(std::random_device{}());
            std::uniform_real_distribution<Sample> distribution(-1.0, 1.0);
            _output.push(distribution(generator));
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
            topological_sort_with_cycles();
            init_buffers();
        }

    private:
        /*  ▄▄▄▄▄  Graph::topological_sort_with_cycles  ▄▄▄▄▄
            Visits every node exactly once in *breadth‑first*
            order, starting from the nodes that are reached
            first when you fan‑out from the graph’s external
            outputs, as well as nodes with no inupts.
        */
        void topological_sort_with_cycles() {
            const size_t n = _nodes.size();

            /* — map every InputPort* back to its owner node index — */
            // note: do not include this graph so we do not cross the boundary of the graph itself
            std::unordered_map<InputPort*, size_t> owner;
            for (size_t i = 0; i < n; ++i) {
                for (auto& in : _nodes[i]->inputs()) {
                    owner[&in] = i;
                }
            }

            std::unordered_map<InputPort*, OutputPort*> connections;
            for (size_t i = 0; i < n; ++i) {
                for (auto& out : _nodes[i]->outputs()) {
                    for (auto const dst : out.fan) {
                        connections[dst] = &out;
                    }
                }
            }

            /* — breadth‑first order — */
            std::deque<size_t> queue;

            /* 1. seed with every node with all inputs deconnected (true sources) */
            for (size_t i = 0; i < n; ++i) {
                bool is_source = true;
                for (auto& in : _nodes[i]->inputs()) {
                    if (connections.contains(&in)) {
                        is_source = false;
                        break;
                    }
                }
                if (!is_source) continue;
                queue.push_back(i);
            }

            /* 2. seed with nodes that read directly from *graph* inputs
                  (i.e. any consumer of an OutputPort in `private_outs`)          */
            for (auto& out : _private_outs) {
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
                        input_global_latencies[dst] = node_global_latency + out.latency();
                    }
                }
            }
        }

    public:
        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            for (size_t i = 0; i < _private_outs.size(); ++i) {
                _private_outs[i].push(_public_ins[i].next());
            }
            for (auto& node : _nodes) {
                node->tick(midi);
            }
            for (size_t i = 0; i < _private_ins.size(); ++i) {
                _public_outs[i].push(_private_ins[i].next());
            }
        }

        size_t inner_latency() const noexcept override {
            std::unordered_map<InputPort const*, size_t> arrival;
            size_t graph_latency = 0;

            for (auto const& node : _nodes) {
                size_t here = 0;
                for (auto const& in : node->inputs()) {
                    here = std::max(here, arrival[&in]);
                }
                here += node->inner_latency();
                for (auto const& out : node->outputs()) {
                    size_t out_latency = here + out.latency();
                    for (auto* dst : out.fan) {
                        arrival[dst] = std::max(arrival[dst], out_latency);
                    }
                }
            }

            for (auto const& sink : _private_ins) {
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

            // note: number of private inputs corresponds to the number of public outputs
            // note: number of private outputs corresponds to the number of public inputs
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

        size_t add_input_port() noexcept {
            return num_inputs++;
        }

        size_t add_output_port() noexcept {
            return num_outputs++;
        }

        void connect(NodePortId source, NodePortId destination) {
            edges.emplace_back(source, destination);
        }
    };

    struct MidiNode : public Node {
        static constexpr size_t const MAX_MIDI_NOTES = 128;

        struct MidiNoteState {
            size_t ttl{0};
            size_t amplitude{0};
        };

        std::array<std::unique_ptr<Node>, MAX_MIDI_NOTES> _graphs;
        std::array<MidiNoteState, MAX_MIDI_NOTES> _note_states;
        size_t _max_ttl;
        InputPort in_silence_threshold;
        OutputPort out_mix;

        explicit MidiNode(NodeFactory<Graph> const& voice_factory) noexcept:
            in_silence_threshold(std::pow(10.0, -60.0 / 20.0)) // -60db
        {
            for (size_t i = 0; i < MAX_MIDI_NOTES; ++i) {
                _graphs[i] = voice_factory.create();
            }
            _max_ttl = dynamic_cast<Graph*>(_graphs[0].get())->inner_latency();
        }

        void tick(std::span<MidiMessage const> const& midi) noexcept override {
            auto silence_threshold = in_silence_threshold.next();

            for (auto const& midi_message : midi) {
                if (midi_message.type == MidiMessageType::NOTE_ON) {
                    auto& note_state = _note_states[midi_message.note_on.note_number];
                    note_state.amplitude = midi_message.note_on.amplitude;
                    note_state.ttl = _max_ttl;
                }
                if (midi_message.type == MidiMessageType::NOTE_OFF) {
                    auto& note_state = _note_states[midi_message.note_off.note_number];
                    note_state.amplitude = 0;
                }
            }

            Sample result = 0.0;
            for (size_t note_number = 0; note_number < MAX_MIDI_NOTES; ++note_number) {
                auto& note_state = _note_states[note_number];
                if (!note_state.ttl) continue;

                auto& graph = _graphs[note_number];
                auto graph_last_out = graph->outputs()[0].back();
                if (note_state.amplitude) {
                    note_state.ttl = _max_ttl;
                }
                else if (note_state.ttl) {
                    --note_state.ttl;
                }
                else if (graph_last_out <= silence_threshold && graph_last_out >= -silence_threshold) continue;

                auto const graph_inputs = graph->inputs();
                graph_inputs[0].update(note_number_to_frequency(note_number));
                graph_inputs[1].update(note_state.amplitude / 127.0);
                graph->tick(midi);
                result += graph->outputs()[0].back();
            }
            out_mix.push(result);
        }

        size_t inner_latency() const noexcept override {
            return _graphs[0]->inner_latency();
        }

        static Sample note_number_to_frequency(uint8_t note_number) {
            return Sample(440.0 * std::pow(2.0, (note_number - 69) / 12.0));
        }

        std::span<InputPort const> inputs_impl() const noexcept override { return { &in_silence_threshold, 1 }; }
        std::span<OutputPort const> outputs_impl() const noexcept override { return { &out_mix, 1 }; }
    };

    template<>
    class NodeFactory<MidiNode> : public NodeFactoryBaseTemplate<MidiNode> {
        NodeFactory<Graph> _voice_factory;
        PortId _frequency_port, _amplitude_port;
        PortId _output_port;

    public:
        
        explicit NodeFactory() noexcept:
            _frequency_port(_voice_factory.add_input_port()),
            _amplitude_port(_voice_factory.add_input_port()),
            _output_port(_voice_factory.add_output_port())
        {}

        std::unique_ptr<MidiNode> create_t() const override {
            return std::make_unique<MidiNode>(_voice_factory);
        }

        std::unique_ptr<NodeFactoryBase> clone() const override {
            auto factory = std::make_unique<NodeFactory>();
            factory->_voice_factory = _voice_factory;
            factory->_frequency_port = _frequency_port;
            factory->_amplitude_port = _amplitude_port;
            factory->_output_port = _output_port;
            return factory;
        }

        NodeFactory<Graph>& get_voice_factory() {
            return _voice_factory;
        }

        PortId get_frequency_port() const noexcept {
            return _frequency_port;
        }

        PortId get_amplitude_port() const noexcept {
            return _amplitude_port;
        }

        PortId get_output_port() const noexcept {
            return _output_port;
        }
    };
}
