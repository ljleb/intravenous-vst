#pragma once
#include "basic_nodes.h"
#include "node.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <variant>
#include <numeric>


namespace iv {
    struct PortId {
        size_t node;
        size_t port;

        bool operator==(PortId const&) const noexcept = default;
    };

    struct GraphEdge {
        PortId source, target;

        bool operator==(GraphEdge const&) const noexcept = default;
    };
}

template<>
struct std::hash<iv::PortId>
{
    std::hash<size_t> size_t_hash;
    std::size_t operator()(const iv::PortId& p) const noexcept
    {
        return size_t_hash(p.node) ^ (~size_t_hash(p.port) - 1);
    }
};

template<>
struct std::hash<iv::GraphEdge> {
    std::hash<iv::PortId> port_id_hash;
    std::size_t operator()(const iv::GraphEdge& e) const noexcept
    {
        return port_id_hash(e.source) ^ (~port_id_hash(e.target) - 1);
    }
};

namespace iv {
    static size_t calculate_port_buffer_size(size_t latency, size_t history) noexcept
    {
        size_t min_size = 1 + latency + history;
        size_t pow2_size = size_t(1) << size_t(std::ceil(std::log2(min_size)));
        return pow2_size;
    };

    struct GraphNode {
        using Nodes = std::vector<TypeErasedNode>;
        using Edges = std::unordered_set<GraphEdge>;

        struct GraphState : public NodeState {
            std::span<NodeState> node_states;
        };

        Nodes _nodes;
        Edges _edges;
        size_t _num_public_inputs;
        size_t _num_public_outputs;

    public:
        static constexpr size_t GRAPH_ID = std::numeric_limits<size_t>::max();

        explicit GraphNode(Nodes nodes, Edges edges, size_t num_inputs = 0, size_t num_outputs = 0) :
            _nodes(std::move(nodes)),
            _edges(std::move(edges)),
            _num_public_inputs(num_inputs),
            _num_public_outputs(num_outputs)
        {
            expand_hyperedge_ports();
            sort_nodes();
            validate_graph();
        }

        constexpr auto inputs() const noexcept
        {
            return std::vector<InputConfig>(_num_public_inputs);
        }

        constexpr auto outputs() const noexcept
        {
            return std::vector<OutputConfig>(_num_public_outputs);
        }

        constexpr auto num_inputs() const noexcept
        {
            return _num_public_inputs;
        }

        constexpr auto num_outputs() const noexcept
        {
            return _num_public_outputs;
        }

        template<typename Allocator>
        void init_buffer(Allocator& allocator) const
        {
            /*
            * struct MemoryLayout {
            *     GraphState;    // private inputs outputs pointers
            *     NodeState[n];
            *     OutputPort[pi];  // private outputs connected to public inputs
            *     struct {
            *         // i, o, s and b specific to each node
            *         SharedPortData[i];
            *         OutputPort[o];
            *         InputPort[i];
            *
            *         // input port samples, not output port samples
            *         // honestly not sure which of the two the samples buffer should be stored with
            *         Sample[s];
            *         std::byte[b];
            *     }[n];
            *     SharedPortData[po];
            *     InputPort[po];  // private inputs connected to public outputs
            *     Sample[ps];     // samples for private inputs
            * };
            */

            size_t num_nodes = _nodes.size();

            std::vector<InputConfig> private_input_configs(_num_public_outputs);
            OutputConfig private_outputs_config;

            GraphState& graph_state = allocator.initialize_object<GraphState>();
            allocator.assign(graph_state.node_states, allocator.initialize_array<NodeState>(num_nodes));
            allocator.assign(graph_state.outputs, allocator.allocate_array<OutputPort>(_num_public_inputs));

            std::unordered_map<PortId, PortId> source_of;
            std::unordered_map<PortId, PortId> target_of;
            for (GraphEdge const& edge : _edges)
            {
                source_of[edge.target] = edge.source;
                target_of[edge.source] = edge.target;
            }

            std::unordered_map<PortId, size_t> input_port_global_latencies;
            std::unordered_map<GraphEdge, size_t> corrected_latencies;
            std::unordered_map<PortId, std::span<Sample>> input_ports_samples;
            std::unordered_map<size_t, std::span<SharedPortData>> node_port_data;

            auto allocate_node_state = [&](auto const& node, size_t node_i)
            {
                NodeState& node_state = (node_i == GRAPH_ID)
                    ? graph_state
                    : allocator.at(graph_state.node_states, node_i);

                std::span<InputConfig const> input_configs;
                std::span<OutputConfig const> output_configs;
                if (node_i == GRAPH_ID)
                {
                    // private inputs
                    input_configs = private_input_configs;
                    // no outputs, public are handled by parent node and private are handled by the children nodes connected to them
                }
                else
                {
                    input_configs = get_inputs(node);
                    output_configs = get_outputs(node);
                }
                size_t const num_inputs = input_configs.size();
                size_t const num_outputs = output_configs.size();
                node_port_data[node_i] = allocator.allocate_array<SharedPortData>(num_inputs);
                if (node_i != GRAPH_ID) allocator.assign(node_state.outputs, allocator.allocate_array<OutputPort>(num_outputs));
                allocator.assign(node_state.inputs, allocator.allocate_array<InputPort>(num_inputs));

                // align latencies
                size_t node_global_latency = 0;
                std::vector<size_t> input_port_extra_latencies(num_inputs);
                for (size_t in_port = 0; in_port < num_inputs; ++in_port)
                {
                    node_global_latency = std::max(node_global_latency, input_port_global_latencies[{ node_i, in_port }]);
                }
                for (size_t in_port = 0; in_port < num_inputs; ++in_port)
                {
                    input_port_extra_latencies[in_port] += node_global_latency - input_port_global_latencies[{ node_i, in_port }];
                }
                if (node_i != GRAPH_ID) {
                    node_global_latency += get_internal_latency(node);
                    for (size_t out_port = 0; out_port < num_outputs; ++out_port)
                    {
                        size_t output_latency = node_global_latency + output_configs[out_port].latency;
                        PortId connection = target_of[{ node_i, out_port }];
                        input_port_global_latencies[connection] = output_latency;
                    }
                }

                for (size_t input_i = 0; input_i < num_inputs; ++input_i)
                {
                    PortId this_input{ node_i, input_i };
                    InputConfig const& input_config = input_configs[input_i];
                    size_t num_port_samples;

                    if (auto it = source_of.find(this_input); it != source_of.end())
                    {
                        // input is connected to output, let's setup both
                        size_t output_node_i = it->second.node;
                        size_t output_port_i = it->second.port;
                        GraphEdge this_edge{ it->second, this_input };

                        OutputConfig const& output_config = (output_node_i == GRAPH_ID)
                            ? private_outputs_config
                            : get_outputs(_nodes[output_node_i])[output_port_i];

                        size_t const corrected_latency = output_config.latency + input_port_extra_latencies[input_i];
                        corrected_latencies.insert({ this_edge, corrected_latency });
                        num_port_samples = calculate_port_buffer_size(corrected_latency, input_config.history);
                    }
                    else
                    {
                        // input is disconnected, no corresponding output port
                        num_port_samples = calculate_port_buffer_size(0, input_config.history);
                    }

                    input_ports_samples.insert({ this_input, allocator.allocate_array<Sample>(num_port_samples) });
                }

                if (node_i != GRAPH_ID)
                {
                    CountingNonAllocator counter(allocator.get_buffer().data());
                    do_init_buffer(node, counter);
                    allocator.assign(node_state.buffer, allocator.allocate_array<std::byte>(counter.estimate_buffer_size()));
                }
            };

            for (size_t node_i = 0; node_i < num_nodes + 1; ++node_i)
            {
                if (node_i < num_nodes)
                {
                    allocate_node_state(_nodes[node_i], node_i);
                }
                else if (node_i == num_nodes)
                {
                    allocate_node_state(*this, GRAPH_ID);
                }
            }

            if (allocator.can_allocate())
            {
                // memory was allocated, now let's initialize it
                auto initialize_node_state = [&](auto const& node, size_t node_i)
                {
                    NodeState& node_state = (node_i == GRAPH_ID)
                        ? graph_state
                        : allocator.at(graph_state.node_states, node_i);

                    std::span<InputConfig const> input_configs;
                    std::span<OutputConfig const> output_configs;
                    if (node_i == GRAPH_ID)
                    {
                        // private inputs
                        input_configs = private_input_configs;
                        // no outputs, public are handled by parent node and private are handled by the children nodes connected to them
                    }
                    else
                    {
                        input_configs = get_inputs(node);
                        output_configs = get_outputs(node);
                    }
                    size_t const num_inputs = input_configs.size();
                    size_t const num_outputs = output_configs.size();
                    std::span<SharedPortData> inputs_port_data = node_port_data[node_i];

                    for (size_t input_i = 0; input_i < num_inputs; ++input_i)
                    {
                        PortId this_input{ node_i, input_i };
                        InputConfig const& input_config = input_configs[input_i];
                        InputPort& input_port = allocator.at(node_state.inputs, input_i);
                        SharedPortData& input_port_data = allocator.at(inputs_port_data, input_i);
                        std::span<Sample> port_samples = input_ports_samples.at(this_input);

                        if (input_config.default_value)
                        {
                            allocator.fill_n(port_samples, input_config.default_value);
                        }

                        if (auto it = source_of.find({ node_i, input_i }); it != source_of.end())
                        {
                            // input is connected to output, let's setup both
                            size_t output_node_i = it->second.node;
                            size_t output_port_i = it->second.port;
                            GraphEdge this_edge{ it->second, this_input };

                            OutputPort& output_port = (output_node_i == GRAPH_ID)
                                ? allocator.at(graph_state.outputs, output_port_i)
                                : allocator.at(allocator.at(graph_state.node_states, output_node_i).outputs, output_port_i);

                            size_t const corrected_latency = corrected_latencies[this_edge];
                            allocator.construct_at(&input_port_data, port_samples, corrected_latency);
                            allocator.construct_at(&output_port, input_port_data);
                        }
                        else
                        {
                            // input is disconnected: init dummy buffer
                            allocator.construct_at(&input_port_data, port_samples, 0);
                        }
                        allocator.construct_at(&input_port, input_port_data, input_config.history);
                    }

                    if (node_i != GRAPH_ID)
                    {
                        FixedBufferAllocator nested_allocator(node_state.buffer);
                        std::span<std::byte> result = do_init_buffer(node, nested_allocator);
                        assert(result.data() == node_state.buffer.data() && result.size() == node_state.buffer.size());
                    }
                };

                for (size_t node_i = 0; node_i < num_nodes + 1; ++node_i)
                {
                    if (node_i < num_nodes)
                    {
                        initialize_node_state(_nodes[node_i], node_i);
                    }
                    else if (node_i == num_nodes)
                    {
                        initialize_node_state(*this, GRAPH_ID);
                    }
                }
            }
        }

        void tick(TickState const& state) noexcept
        {
            auto& private_state = get_private_state(state.buffer);
            for (size_t i = 0; i < _num_public_inputs; ++i) {
                private_state.outputs[i].push(state.inputs[i].get());
            }
            size_t num_nodes = _nodes.size();
            for (size_t i = 0; i < num_nodes; ++i)
            {
                _nodes[i].tick({
                    private_state.node_states[i],
                    state.midi,
                    });
            }
            for (size_t i = 0; i < _num_public_outputs; ++i) {
                state.outputs[i].push(private_state.inputs[i].get());
            }
        }

        GraphState& get_private_state(std::span<std::byte> buffer) const
        {
            void* object = buffer.data();
            size_t space = buffer.size();
            return *reinterpret_cast<GraphState*>(std::align(alignof(GraphState), sizeof(GraphState), object, space));  // first index in the buffer
        }

        size_t internal_latency() const noexcept
        {
            std::unordered_map<PortId, PortId> target_of;
            for (GraphEdge const& edge : _edges)
            {
                target_of[edge.source] = edge.target;
            }

            std::unordered_map<PortId, size_t> input_global_latencies;
            size_t max_latency = 0;

            auto process_node = [&](auto& node, size_t node_i)
                {
                    size_t node_global_latency = 0;

                    size_t const num_inputs = (node_i != GRAPH_ID) ? get_num_inputs(node) : _num_public_outputs;
                    for (size_t input_port = 0; input_port < num_inputs; ++input_port)
                    {
                        node_global_latency = std::max(node_global_latency, input_global_latencies[{ node_i, input_port }]);
                    }

                    if (node_i == GRAPH_ID) return;

                    node_global_latency += get_internal_latency(node);
                    size_t const num_outputs = get_num_outputs(node);
                    for (size_t output_port = 0; output_port < num_outputs; ++output_port)
                    {
                        if (auto it = target_of.find({ node_i, output_port }); it != target_of.end())
                        {
                            size_t new_latency = node_global_latency + get_outputs(node)[output_port].latency;
                            max_latency = std::max(max_latency, new_latency);
                            input_global_latencies[it->second] = new_latency;
                        }
                    }
                };

            for (size_t node_i = 0; node_i < _nodes.size() + 1; ++node_i) {
                if (node_i < _nodes.size())
                {
                    process_node(_nodes[node_i], node_i);
                }
                else
                {
                    process_node(*this, GRAPH_ID);
                }
            }
            return max_latency;
        }

    private:
        void expand_hyperedge_ports()
        {
            std::unordered_map<PortId, std::vector<GraphEdge>> reverse_edges_map;
            for (GraphEdge const& edge : _edges)
            {
                reverse_edges_map[edge.target].push_back(edge);
            }

            size_t nodes_size = _nodes.size();
            for (size_t node = 0; node < nodes_size; ++node)
            {
                size_t const num_inputs = get_num_inputs(_nodes[node]);
                for (size_t in_port = 0; in_port < num_inputs; ++in_port)
                {
                    auto it = reverse_edges_map.find({ node, in_port });
                    if (it == reverse_edges_map.end()) continue;
                    auto const& edges_to_expand = it->second;
                    size_t const port_arity = edges_to_expand.size();
                    if (port_arity <= 1) continue;

                    _nodes.emplace_back(SumNode(port_arity));
                    size_t const sum_node = _nodes.size() - 1;

                    for (size_t out_port = 0; out_port < edges_to_expand.size(); ++out_port)
                    {
                        GraphEdge const& to_rewire = edges_to_expand[out_port];
                        _edges.erase(to_rewire);
                        _edges.insert(GraphEdge{ to_rewire.source, { sum_node, out_port } });
                    }
                    _edges.insert(GraphEdge{ { sum_node, 0 }, { node, in_port } });
                }
            }

            std::unordered_map<PortId, std::vector<GraphEdge>> edges_map;
            for (GraphEdge const& edge : _edges)
            {
                edges_map[edge.source].push_back(edge);
            }

            nodes_size = _nodes.size();
            for (size_t node = 0; node < nodes_size; ++node)
            {
                size_t const num_outputs = get_num_outputs(_nodes[node]);
                for (size_t out_port = 0; out_port < num_outputs; ++out_port)
                {
                    auto it = edges_map.find({ node, out_port });
                    if (it == edges_map.end()) continue;
                    auto const& edges_to_expand = it->second;
                    size_t const port_arity = edges_to_expand.size();
                    if (port_arity <= 1) continue;

                    _nodes.emplace_back(BroadcastNode(port_arity));
                    size_t const broadcast_node = _nodes.size() - 1;

                    for (size_t in_port = 0; in_port < edges_to_expand.size(); ++in_port)
                    {
                        GraphEdge const& to_rewire = edges_to_expand[in_port];
                        _edges.erase(to_rewire);
                        _edges.insert(GraphEdge{ { broadcast_node, in_port }, to_rewire.target });
                    }
                    _edges.insert(GraphEdge{ { node, out_port }, { broadcast_node, 0 } });
                }
            }
        }

        void sort_nodes()
        {
            const size_t n = _nodes.size();

            std::unordered_map<PortId, PortId> forward_edges_map;
            std::unordered_map<PortId, PortId> backward_edges_map;
            for (GraphEdge const& edge : _edges)
            {
                forward_edges_map[edge.source] = edge.target;
                backward_edges_map[edge.target] = edge.source;
            }

            auto make_heads_queue = [&]()
            {
                std::deque<size_t> queue;

                // include all nodes with 0 connected input ports
                for (size_t node = 0; node < n; ++node)
                {
                    bool all_inputs_disconnected = true;
                    size_t const num_inputs = get_num_inputs(_nodes[node]);
                    for (size_t input_port = 0; input_port < num_inputs; ++input_port)
                    {
                        if (backward_edges_map.contains({ node, input_port }))
                        {
                            all_inputs_disconnected = false;
                            break;
                        }
                    }
                    if (all_inputs_disconnected) queue.push_back(node);
                }
                // include all nodes directly connected to the source ports of the graph
                for (size_t private_out_i = 0; private_out_i < _num_public_inputs; ++private_out_i)
                {
                    if (auto it = forward_edges_map.find({ GRAPH_ID, private_out_i }); it != forward_edges_map.end())
                    {
                        queue.push_back(it->second.node);
                    }
                }
                return queue;
            };

            // traverse the graph for each node to find their respective cyclic parents
            std::unordered_map<size_t, std::unordered_set<size_t>> cyclic_parents_of(n);
            {
                auto queue = make_heads_queue();
                std::vector<bool> seen(n, false);
                while (!queue.empty())
                {
                    size_t initial_node = queue.front();
                    queue.pop_front();
                    if (initial_node == GRAPH_ID || seen[initial_node]) continue;
                    seen[initial_node] = true;

                    std::vector<bool> inner_seen(n, false);
                    std::deque<size_t> inner_queue;
                    inner_queue.push_back(initial_node);

                    while (!inner_queue.empty())
                    {
                        size_t node = inner_queue.front();
                        inner_queue.pop_front();
                        if (node == GRAPH_ID || inner_seen[node]) continue;
                        inner_seen[node] = true;

                        // if we run into a node that already has cycles, then we are in a cycle and this cycle was already resolved
                        if (!cyclic_parents_of[node].empty()) continue;

                        size_t const num_outputs = get_num_outputs(_nodes[node]);
                        for (size_t out_i = 0; out_i < num_outputs; ++out_i)
                        {
                            if (auto it = forward_edges_map.find({ node, out_i }); it != forward_edges_map.end())
                            {
                                size_t child = it->second.node;
                                if (child == initial_node)
                                {
                                    cyclic_parents_of[initial_node].insert(node);
                                }
                                else
                                {
                                    inner_queue.push_back(child);
                                }
                            }
                        }
                    }

                    size_t const num_outputs = get_num_outputs(_nodes[initial_node]);
                    for (size_t out_i = 0; out_i < num_outputs; ++out_i) {
                        if (auto it = forward_edges_map.find({ initial_node, out_i }); it != forward_edges_map.end())
                        {
                            queue.push_back(it->second.node);
                        }
                    }
                }
            }

            auto queue = make_heads_queue();
            std::vector<bool> placed(n, false);
            std::vector<size_t> sorted;
            sorted.reserve(n);

            while (!queue.empty())
            {
                size_t node = queue.front();
                queue.pop_front();
                if (node == GRAPH_ID || placed[node]) continue;

                bool all_dependencies_satisfied = true;
                size_t const num_inputs = get_num_inputs(_nodes[node]);
                for (size_t in_i = 0; in_i < num_inputs; ++in_i) {
                    if (auto it = backward_edges_map.find({ node, in_i }); it != backward_edges_map.end())
                    {
                        size_t parent = it->second.node;
                        if (parent != GRAPH_ID && !placed[parent] && !cyclic_parents_of[node].contains(parent)) {
                            all_dependencies_satisfied = false;
                            break;
                        }
                    }
                }

                if (all_dependencies_satisfied) {
                    size_t const num_outputs = get_num_outputs(_nodes[node]);
                    for (size_t out_i = 0; out_i < num_outputs; ++out_i)
                    {
                        if (auto it = forward_edges_map.find({ node, out_i }); it != forward_edges_map.end())
                        {
                            queue.push_back(it->second.node);
                        }
                    }
                    sorted.push_back(node);
                    placed[node] = true;
                }
                else {
                    queue.push_back(node);
                }
            }

            Nodes sorted_nodes;
            sorted_nodes.reserve(n);
            for (size_t old_i = 0; old_i < n; ++old_i)
            {
                sorted_nodes.push_back(std::move(_nodes[sorted[old_i]]));
            }
            _nodes.swap(sorted_nodes);

            std::vector<size_t> reverse_sorted(n);
            std::ranges::iota(reverse_sorted, 0);
            std::ranges::stable_sort(reverse_sorted, [&](int i1, int i2) {return sorted[i1] < sorted[i2]; });

            Edges sorted_edges;
            sorted_edges.reserve(_edges.size());
            for (GraphEdge edge : _edges)
            {
                if (edge.source.node != GRAPH_ID)
                    edge.source.node = reverse_sorted[edge.source.node];
                if (edge.target.node != GRAPH_ID)
                    edge.target.node = reverse_sorted[edge.target.node];
                sorted_edges.insert(edge);
            }
            _edges.swap(sorted_edges);
        }

        void validate_graph() const
        {
            std::vector<InputConfig> _private_input_configs(_num_public_outputs);
            std::vector<OutputConfig> _private_output_configs(_num_public_inputs);

            for (auto const& edge : _edges)
            {
                std::span<OutputConfig const> source_outputs;
                if (edge.source.node == GRAPH_ID)
                {
                    source_outputs = _private_output_configs;
                }
                else
                {
                    source_outputs = get_outputs(_nodes[edge.source.node]);
                }
                std::span<InputConfig const> target_inputs;
                if (edge.target.node == GRAPH_ID)
                {
                    target_inputs = _private_input_configs;
                }
                else
                {
                    target_inputs = get_inputs(_nodes[edge.target.node]);
                }
                assert(edge.source.port < source_outputs.size() && "bad connection");
                assert(edge.target.port < target_inputs.size() && "bad connection");
            }
        }
    };

    struct alignas(max_align_t) AlignedBytes {
        std::byte b[alignof(max_align_t)];
    };

    class NodeProcessor {
        using Buffer = std::vector<AlignedBytes>;

        GraphNode _node;
        Buffer _buffer;
        NodeState _graph_state;

        void resize_buffer()
        {
            _buffer.reserve(1);
            std::byte* byte_data = reinterpret_cast<std::byte*>(_buffer.data());
            CountingNonAllocator counter(byte_data);
            do_init_buffer(_node, counter);
            _buffer.resize(counter.estimate_buffer_size());
        }

        void initialize_graph()
        {
            FixedBufferAllocator allocator({
                reinterpret_cast<std::byte*>(_buffer.data()),
                _buffer.size() * sizeof(AlignedBytes)
                });
            auto allocated = do_init_buffer(_node, allocator);
            assert(
                _buffer.size() - allocated.size() < alignof(max_align_t) &&
                "buffer was over allocated"
            );
        }

    public:
        explicit NodeProcessor(GraphNode node) noexcept :
            _node(std::move(node))
        {
            assert(get_num_inputs(_node) == 0 && "the graph should have 0 inputs");
            assert(get_num_outputs(_node) == 0 && "the graph should have 0 outputs");

            resize_buffer();
            initialize_graph();
        }

        void tick(std::span<MidiMessage const> midi) noexcept
        {
            std::span<std::byte> buffer_span{
                reinterpret_cast<std::byte*>(_buffer.data()),
                _buffer.size() * sizeof(AlignedBytes)
            };
            _node.tick({ NodeState {.buffer = buffer_span }, midi });
        }
    };
}
