#include "pch.h"
#include "public.h"
#include "midi_node.h"
#include "graph_node.h"
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

struct WhackIirThing {
    double const* _update_frequency;

public:
    constexpr explicit WhackIirThing(double const* update_frequency) noexcept :
        _update_frequency(update_frequency)
    {}

    constexpr auto inputs() const noexcept
    {
        return std::array<iv::InputConfig, 3>{};
    }

    constexpr auto outputs() const noexcept
    {
        return std::array<iv::OutputConfig, 2>{};
    }

    void tick(iv::TickState const& state) const noexcept {
        iv::Sample in_dry = state.inputs[0].get();
        iv::Sample in_control = state.inputs[1].get();
        iv::Sample dx = *_update_frequency;
        iv::Sample alpha = 1 - in_control; for (size_t i = 0; i < 4; ++i) alpha *= alpha;

        auto& out_low = state.outputs[0];
        auto& out_high = state.outputs[1];
        iv::Sample last_low = out_low.get();

        iv::Sample low = last_low + alpha * (in_dry - last_low);
        out_low.push(low);
        out_high.push(in_dry - low);
    }
};

struct SimpleIirHighPass {
    double const* _sample_period;

public:
    static constexpr iv::Sample const FMIN = 1;
    static constexpr iv::Sample const FMAX = 4.41e4;

    constexpr explicit SimpleIirHighPass(double const* sample_period) noexcept :
        _sample_period(sample_period)
    {}

    constexpr auto inputs() const noexcept
    {
        return std::array {
            iv::InputConfig { .history = 1 },
            iv::InputConfig{},
        };
    }

    constexpr auto outputs() const noexcept
    {
        return std::array<iv::OutputConfig, 1>{};
    }

    void tick(iv::TickState const& state) const noexcept {
        auto& in = state.inputs[0];
        auto& ctrl = state.inputs[1];
        auto& out = state.outputs[0];

        // read your 0…1 knob
        iv::Sample u = ctrl.get();                   // ∈ [0,1]

        // compute host rate and the *usable* max cutoff
        iv::Sample dx = *_sample_period;
        iv::Sample usableMax = std::min<iv::Sample>(FMAX, 0.5 / dx);

        // 2) (optionally) exponential sweep, clamped
        iv::Sample f_c = FMIN * std::pow(usableMax / FMIN, u);

        // normalize and compute IIR coeffs
        iv::Sample norm = f_c * dx;                     // ∈ [0, usableMax/fs] ⩽ 0.5
        iv::Sample c = std::tan(std::_Pi_val * norm);
        iv::Sample a1 = (1.0 - c) / (1.0 + c);
        iv::Sample b0 = 1.0 / (1.0 + c);

        // standard one‑pole highpass:
        iv::Sample x = in.get(0);
        iv::Sample x1 = in.get(1);
        iv::Sample y1 = out.get();
        iv::Sample y = a1*y1 + b0*(x - x1);

        out.push(y);
    }
};

struct SimpleIirLowPass {
    double const* _sample_period;

public:
    static constexpr iv::Sample const FMIN = 2e1;
    static constexpr iv::Sample const FMAX = 4.41e4;

    constexpr explicit SimpleIirLowPass(double const* sample_period) noexcept :
        _sample_period(sample_period)
    {}

    constexpr auto inputs() const noexcept
    {
        return std::array {
            iv::InputConfig { .history = 1 },
            iv::InputConfig{},
        };
    }

    constexpr auto outputs() const noexcept
    {
        return std::array<iv::OutputConfig, 1>{};
    }

    void tick(iv::TickState const& state) const noexcept {
        auto& in = state.inputs[0];
        auto& ctrl = state.inputs[1];
        auto& out = state.outputs[0];

        // 1) read your 0…1 knob
        iv::Sample u = ctrl.get();          // ∈ [0,1]

        // 2) compute sample‐rate and clamped max cutoff
        iv::Sample dx = *_sample_period;
        iv::Sample usableMax = std::min<iv::Sample>(FMAX, 0.5 / dx);

        // 3a) linear sweep: maps 0→1 straight to FMIN→usableMax
        //iv::Sample fc_linear = FMIN + u * (usableMax - FMIN);

        // 3b) exponential sweep: perceptually smoother
        iv::Sample f_c = FMIN * std::pow(usableMax / FMIN, u);

        // 4) normalize to [0…0.5] and compute warped c
        iv::Sample norm = f_c * dx;             // now ∈ [FMIN/fs … usableMax/fs] ⩽ 0.5
        iv::Sample c = std::tan(std::_Pi_val * norm);

        // 5) one‑pole low‑pass coeffs
        iv::Sample a1 = (1.0 - c) / (1.0 + c);
        iv::Sample alpha = c / (1.0 + c);

        // 6) apply difference equation
        iv::Sample x = in.get(0);
        iv::Sample x_prev = in.get(1);
        iv::Sample y_prev = out.get();

        iv::Sample y = a1*y_prev + alpha*(x + x_prev);

        out.push(y);
    }
};

size_t iv::init_graph(
    double* update_frequency,
    Sample* channels[2],
    std::atomic<float>* warp_threshold,
    std::atomic<float>* noise_level,
    std::atomic<float>* noise_lo_pass,
    std::atomic<float>* noise_hi_pass
) noexcept {
    auto graph = make_subgraph([=](auto& nodes, auto& edges)
    {
        constexpr auto graph = iv::GRAPH_ID;
        auto warp_threshold_knob = emplace_back_id(nodes, KnobNode<float>(warp_threshold));
        //auto iir_outw0_knob = emplace_back_id(nodes, KnobNode<float>(_iir_outw0));

        auto noise = make_subgraph_id(nodes, [&](auto& nodes, auto& edges)
        {
            auto level_knob = emplace_back_id(nodes, KnobNode<float>(noise_level));
            auto lo_pass_knob = emplace_back_id(nodes, KnobNode<float>(noise_lo_pass));
            auto hi_pass_knob = emplace_back_id(nodes, KnobNode<float>(noise_hi_pass));
            auto generator = emplace_back_id(nodes, iv::UniformNoiseNode());
            auto product = emplace_back_id(nodes, iv::ProductNode());
            auto lo_pass = emplace_back_id(nodes, SimpleIirLowPass(update_frequency));
            auto hi_pass = emplace_back_id(nodes, SimpleIirHighPass(update_frequency));

            edges.insert(iv::GraphEdge { { generator,    0 }, { lo_pass, 0 } });
            edges.insert(iv::GraphEdge { { lo_pass_knob, 0 }, { lo_pass, 1 } });
            edges.insert(iv::GraphEdge { { lo_pass,      0 }, { hi_pass, 0 } });
            edges.insert(iv::GraphEdge { { hi_pass_knob, 0 }, { hi_pass, 1 } });
            edges.insert(iv::GraphEdge { { hi_pass,      0 }, { product, 0 } });
            edges.insert(iv::GraphEdge { { level_knob,   0 }, { product, 1 } });
            edges.insert(iv::GraphEdge { { product,      0 }, { graph,   0 } });

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

                edges.insert(iv::GraphEdge { { graph,    0 }, { product, 0 } });
                edges.insert(iv::GraphEdge { { constant, 0 }, { product, 1 } });
                edges.insert(iv::GraphEdge { { product,  0 }, { graph,   0 } });

                return std::make_tuple(1, 1);
            });

            edges.insert(iv::GraphEdge { { graph, frequency_port }, { frequency, 0 } });

            // main loop
            edges.insert(iv::GraphEdge { { integrator, 0 }, { warper,     0 } });
            edges.insert(iv::GraphEdge { { warper,     1 }, { integrator, 0 } });

            // knobs
            edges.insert(iv::GraphEdge { { frequency, 0 },                          { integrator, 1 } });
            edges.insert(iv::GraphEdge { { graph,     voice_warp_threshold_port },  { warper,     1 } });
            edges.insert(iv::GraphEdge { { graph,     voice_noise_generator_port }, { warper,     0 } });

            // out
            auto amplitude = emplace_back_id(nodes, iv::ProductNode());
            edges.insert(iv::GraphEdge { { graph,     amplitude_port }, { amplitude, 0 } });
            edges.insert(iv::GraphEdge { { warper,    0 },              { amplitude, 1 } });
            edges.insert(iv::GraphEdge { { amplitude, 0 },              { graph,     0 } });

            return std::make_tuple(4, 1);
        });

        auto midi_left = emplace_back_id(nodes, iv::MidiNode(midi_voice));
        auto midi_right = emplace_back_id(nodes, iv::MidiNode(midi_voice));

        auto left_out = emplace_back_id(nodes, AudioStreamNode(channels[0]));
        auto right_out = emplace_back_id(nodes, AudioStreamNode(channels[1]));

        // shared inputs
        edges.insert(iv::GraphEdge { { warp_threshold_knob, 0 }, { midi_left, 0 } });
        edges.insert(iv::GraphEdge { { noise,               0 }, { midi_left, 1 } });

        edges.insert(iv::GraphEdge { { warp_threshold_knob, 0 }, { midi_right, 0 } });
        edges.insert(iv::GraphEdge { { noise,               0 }, { midi_right, 1 } });

        edges.insert(iv::GraphEdge { { midi_left,  0 }, { left_out,  0 } });
        edges.insert(iv::GraphEdge { { midi_right, 0 }, { right_out, 0 } });

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
