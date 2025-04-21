#include "pch.h"
#include "NodeGraph.h"
#include <any>


static std::any node_processor_storage;
iv::NodeProcessor* node_processor;

template<typename Node>
size_t emplace_back_id(std::vector<iv::TypeErasedNode>& nodes, Node const& node)
{
    nodes.emplace_back(node);
    return nodes.size() - 1;
};

template<typename Fn>
iv::GraphNode make_subgraph(Fn&& fn)
{
    iv::GraphNode::Nodes local_nodes;
    iv::GraphNode::Edges local_edges;
    auto [ins, outs] = std::forward<Fn>(fn)(local_nodes, local_edges);
    return iv::GraphNode(std::move(local_nodes), std::move(local_edges), ins, outs);
};

template<typename Fn>
size_t make_subgraph_id(iv::GraphNode::Nodes& nodes, Fn&& fn)
{
    return emplace_back_id(nodes, make_subgraph(std::forward<Fn>(fn)));
};

template<class T>
class KnobNode {
    std::atomic<T>* _value;

public:
    explicit KnobNode(std::atomic<T>* value) noexcept :
        _value(value)
    {}

    constexpr auto outputs() const noexcept
    {
        return std::array<iv::OutputConfig, 1>{};
    }

    void tick(iv::TickState const& state) noexcept {
        auto& out = state.outputs[0];
        out.push(_value->load(std::memory_order::relaxed));
    }
};

class AudioStreamNode {
    iv::Sample*& _destination;

public:
    constexpr explicit AudioStreamNode(iv::Sample*& destination) noexcept :
        _destination(destination)
    {}

    constexpr auto inputs() const noexcept
    {
        return std::array<iv::InputConfig, 1>{};
    }

    void tick(iv::TickState const& state) noexcept
    {
        auto& in = state.inputs[0];
        *_destination = in.get();
    }
};

struct SimpleIirLowPass {
    constexpr auto inputs() const noexcept
    {
        return std::array<iv::InputConfig, 2>{};
    }

    constexpr auto outputs() const noexcept
    {
        return std::array<iv::OutputConfig, 1>{};
    }

    void tick(iv::TickState const& state) noexcept {
        iv::Sample in_sample = state.inputs[0].get();
        iv::Sample in_alpha = state.inputs[1].get();
        auto& out = state.outputs[0];
        iv::Sample last_out = out.get();
        out.push(last_out + in_alpha * (in_sample - last_out));
    }
};

size_t iv::init_graph(
    double* update_frequency,
    Sample* channels[2],
    std::atomic<float>* warp_threshold,
    std::atomic<float>* noise_level,
    std::atomic<float>* noise_filter
) noexcept {
    auto graph = make_subgraph([=](auto& nodes, auto& edges)
    {
        constexpr auto graph = iv::GraphNode::GRAPH_ID;
        auto warp_threshold_knob = emplace_back_id(nodes, KnobNode<float>(warp_threshold));
        //auto iir_outw0_knob = emplace_back_id(nodes, KnobNode<float>(_iir_outw0));

        auto noise = make_subgraph_id(nodes, [&](auto& nodes, auto& edges)
        {
            auto level_knob = emplace_back_id(nodes, KnobNode<float>(noise_level));
            auto filter_knob = emplace_back_id(nodes, KnobNode<float>(noise_filter));
            auto generator = emplace_back_id(nodes, iv::UniformNoiseNode());
            auto product = emplace_back_id(nodes, iv::ProductNode());
            auto filter = emplace_back_id(nodes, SimpleIirLowPass());

            edges.insert(iv::GraphEdge{ { generator,   0 }, { filter,  0 } });
            edges.insert(iv::GraphEdge{ { filter_knob, 0 }, { filter,  1 } });
            edges.insert(iv::GraphEdge{ { filter,      0 }, { product, 0 } });
            edges.insert(iv::GraphEdge{ { level_knob,  0 }, { product, 1 } });
            edges.insert(iv::GraphEdge{ { product,     0 }, { graph,   0 } });

            return std::make_tuple(0, 1);
        });

        auto midi_voice = make_subgraph([&](auto& nodes, auto& edges)
        {
            size_t amplitude_port = 0;
            size_t frequency_port = 1;
            size_t voice_warp_threshold_port = 2;
            size_t voice_noise_generator_port = 3;

            auto integrator = emplace_back_id(nodes, iv::Integrator(update_frequency));
            auto warper = emplace_back_id(nodes, iv::WarperNode());

            auto frequency = make_subgraph_id(nodes, [](auto& nodes, auto& edges)
            {
                auto product = emplace_back_id(nodes, iv::ProductNode());
                auto constant = emplace_back_id(nodes, iv::ConstantNode(4.0));

                edges.insert(iv::GraphEdge{ { graph,    0 }, { product, 0 } });
                edges.insert(iv::GraphEdge{ { constant, 0 }, { product, 1 } });
                edges.insert(iv::GraphEdge{ { product,  0 }, { graph,   0 } });

                return std::make_tuple(1, 1);
            });

            edges.insert(iv::GraphEdge{ { graph, frequency_port }, { frequency, 0 } });

            // main loop
            edges.insert(iv::GraphEdge{ { integrator, 0 }, { warper,     0 } });
            edges.insert(iv::GraphEdge{ { warper,     1 }, { integrator, 0 } });

            // knobs
            edges.insert(iv::GraphEdge{ { frequency, 0 },                          { integrator, 1 } });
            edges.insert(iv::GraphEdge{ { graph,     voice_warp_threshold_port },  { warper,     1 } });
            edges.insert(iv::GraphEdge{ { graph,     voice_noise_generator_port }, { warper,     0 } });

            // out
            auto amplitude = emplace_back_id(nodes, iv::ProductNode());
            edges.insert(iv::GraphEdge{ { graph,     amplitude_port }, { amplitude, 0 } });
            edges.insert(iv::GraphEdge{ { warper,    0 },              { amplitude, 1 } });
            edges.insert(iv::GraphEdge{ { amplitude, 0 },              { graph,     0 } });

            return std::make_tuple(4, 1);
        });

        auto midi_left = emplace_back_id(nodes, iv::MidiNode(midi_voice));
        auto midi_right = emplace_back_id(nodes, iv::MidiNode(midi_voice));

        auto left_out = emplace_back_id(nodes, AudioStreamNode(channels[0]));
        auto right_out = emplace_back_id(nodes, AudioStreamNode(channels[1]));

        // shared inputs
        edges.insert(iv::GraphEdge{ { warp_threshold_knob, 0 }, { midi_left, 0 } });
        edges.insert(iv::GraphEdge{ { noise, 0 }, { midi_left, 1 } });

        edges.insert(iv::GraphEdge{ { warp_threshold_knob, 0 }, { midi_right, 0 } });
        edges.insert(iv::GraphEdge{ { noise, 0 }, { midi_right, 1 } });

        edges.insert(iv::GraphEdge{ { midi_left, 0 }, { left_out, 0 } });
        edges.insert(iv::GraphEdge{ { midi_right, 0 }, { right_out, 0 } });

        return std::make_tuple(0, 0);
    });

    size_t const latency = iv::get_internal_latency(graph);
    node_processor_storage = NodeProcessor(std::move(graph));
    node_processor = std::any_cast<NodeProcessor>(&node_processor_storage);
    return latency;
}

void iv::tick(std::span<MidiMessage const> midi) noexcept
{
    node_processor->tick(midi);
}

void iv::free_graph()
{
    node_processor_storage.reset();
}
