#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NodeGraph.h"

juce::String const IntravenousAudioProcessor::WARP_THRESHOLD_ID = "warp_threshold";
juce::String const IntravenousAudioProcessor::NOISE_LEVEL_ID = "noise_level";
juce::String const IntravenousAudioProcessor::IIR_OUTW0_ID = "iir_outw0";

IntravenousAudioProcessor::IntravenousAudioProcessor():
    #ifndef JucePlugin_PreferredChannelConfigurations
        AudioProcessor(BusesProperties()
            #if ! JucePlugin_IsMidiEffect
                #if ! JucePlugin_IsSynth
                    .withInput("Input", juce::AudioChannelSet::stereo(), true)
                #endif
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)
            #endif
        ),
    #endif
    _value_tree_state {
        *this, nullptr, "PARAMETERS", {
            std::make_unique<juce::AudioParameterFloat>(
                WARP_THRESHOLD_ID,
                "Warp Threshold",
                juce::NormalisableRange<float>(0.f, 1.f, .0001f),
                1.f),
            std::make_unique<juce::AudioParameterFloat>(
                NOISE_LEVEL_ID,
                "Noise Level",
                juce::NormalisableRange<float>(0.f, 0.1f, .0001f),
                0.f),
            std::make_unique<juce::AudioParameterFloat>(
                IIR_OUTW0_ID,
                "IIR Out Weight 0",
                juce::NormalisableRange<float>(-1.f, 1.f, .0001f),
                0.f),
        }
    },
    _warp_threshold(_value_tree_state.getRawParameterValue(WARP_THRESHOLD_ID)),
    _noise_level(_value_tree_state.getRawParameterValue(NOISE_LEVEL_ID)),
    _iir_outw0(_value_tree_state.getRawParameterValue(IIR_OUTW0_ID)),
    _node_processor(init_graph())
{}

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
        out.push(_value->load());
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

iv::GraphNode IntravenousAudioProcessor::init_graph() {
    auto graph = make_subgraph([this](auto& nodes, auto& edges)
    {
        constexpr auto graph = iv::GraphNode::GRAPH_ID;
        auto warp_threshold_knob = emplace_back_id(nodes, KnobNode<float>(_warp_threshold));
        //auto iir_outw0_knob = emplace_back_id(nodes, KnobNode<float>(_iir_outw0));

        auto noise = make_subgraph_id(nodes, [this](auto& nodes, auto& edges)
        {
            auto knob = emplace_back_id(nodes, KnobNode<float>(_noise_level));
            auto generator = emplace_back_id(nodes, iv::UniformNoiseNode());
            auto product = emplace_back_id(nodes, iv::ProductNode());

            edges.insert(iv::GraphEdge { { knob,      0 }, { product, 0 } });
            edges.insert(iv::GraphEdge { { generator, 0 }, { product, 1 } });
            edges.insert(iv::GraphEdge { { product,   0 }, { graph,   0 } });

            return std::make_tuple(0, 1);
        });

        auto midi_voice = make_subgraph([this](auto& nodes, auto& edges)
        {
            size_t amplitude_port = 0;
            size_t frequency_port = 1;
            size_t voice_warp_threshold_port = 2;
            size_t voice_noise_generator_port = 3;

            auto integrator = emplace_back_id(nodes, iv::Integrator(&_sample_rate));
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

        auto left_out = emplace_back_id(nodes, AudioStreamNode(_channels[0]));
        auto right_out = emplace_back_id(nodes, AudioStreamNode(_channels[1]));

        // shared inputs
        edges.insert(iv::GraphEdge { { warp_threshold_knob, 0 }, { midi_left, 0 } });
        edges.insert(iv::GraphEdge { { noise, 0 }, { midi_left, 1 } });

        edges.insert(iv::GraphEdge { { warp_threshold_knob, 0 }, { midi_right, 0 } });
        edges.insert(iv::GraphEdge { { noise, 0 }, { midi_right, 1 } });

        edges.insert(iv::GraphEdge { { midi_left, 0 }, { left_out, 0 } });
        edges.insert(iv::GraphEdge { { midi_right, 0 }, { right_out, 0 } });

        return std::make_tuple(0, 0);
    });

    setLatencySamples(int(iv::get_internal_latency(graph)));
    return graph;
}

IntravenousAudioProcessor::~IntravenousAudioProcessor() {
}

const juce::String IntravenousAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool IntravenousAudioProcessor::acceptsMidi() const {
    return JucePlugin_WantsMidiInput;
}

bool IntravenousAudioProcessor::producesMidi() const {
    return JucePlugin_ProducesMidiOutput;
}

bool IntravenousAudioProcessor::isMidiEffect() const {
    return JucePlugin_IsMidiEffect;
}

double IntravenousAudioProcessor::getTailLengthSeconds() const {
    return 0.;
}

int IntravenousAudioProcessor::getNumPrograms() {
    return 1;
}

int IntravenousAudioProcessor::getCurrentProgram() {
    return 0;
}

void IntravenousAudioProcessor::setCurrentProgram(int index) {
}

const juce::String IntravenousAudioProcessor::getProgramName(int index) {
    return {};
}

void IntravenousAudioProcessor::changeProgramName(int index, const juce::String& new_name) {
}

void IntravenousAudioProcessor::prepareToPlay(double sample_rate, int samples_per_block) {
    _sample_rate = sample_rate;
    _midi_buffers.resize(samples_per_block);
    _midi_buffer_sizes.resize(samples_per_block, 0);
    _midi_buffers.shrink_to_fit();
}

void IntravenousAudioProcessor::releaseResources() {
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool IntravenousAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    #if JucePlugin_IsMidiEffect
        juce::ignoreUnused(layouts);
        return true;
    #else
        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
            layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
            return false;

        #if ! JucePlugin_IsSynth
            if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
                return false;
        #endif

        return true;
    #endif
}
#endif

void IntravenousAudioProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi_data) {
    juce::ScopedNoDenormals no_denormals;
    float* const* const write_buffer = audio.getArrayOfWritePointers();
    auto const num_samples = audio.getNumSamples();
    auto const channels = getTotalNumOutputChannels();
    auto const latency = getLatencySamples();

    for (auto const& midi: midi_data) {
        auto const& midi_message = midi.getMessage();
        size_t sample_idx = size_t(midi_message.getTimeStamp() + latency);
        auto& midi_buffer = _midi_buffers[sample_idx];
        auto& buffer_size = _midi_buffer_sizes[sample_idx];
        if (buffer_size >= midi_buffer.size()) continue;  // drop message, no space left
        if (midi_message.isNoteOn() && midi_message.getVelocity() > 0) {
            iv::MidiMessage iv_midi_message { .type = iv::MidiMessageType::NOTE_ON };
            iv_midi_message.note_on.amplitude = midi_message.getVelocity();
            iv_midi_message.note_on.note_number = uint8_t(midi_message.getNoteNumber());
            midi_buffer[buffer_size++] = iv_midi_message;
        }
        else if (midi_message.isNoteOff() || midi_message.isNoteOn() && midi_message.getVelocity() == 0) {
            iv::MidiMessage iv_midi_message { .type = iv::MidiMessageType::NOTE_OFF };
            iv_midi_message.note_off.note_number = uint8_t(midi_message.getNoteNumber());
            midi_buffer[buffer_size++] = iv_midi_message;
        }
    }

    for (size_t channel = 0; channel < channels; ++channel) {
        _channels[channel] = write_buffer[channel];
    }

    for (size_t sample = 0; sample < num_samples; ++sample) {
        _node_processor.tick({ _midi_buffers[sample].data(), _midi_buffer_sizes[sample] });
        for (size_t channel = 0; channel < channels; ++channel) {
            ++_channels[channel];
        }
        _midi_buffer_sizes[sample] = 0;
    }
}

void IntravenousAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) {

}

bool IntravenousAudioProcessor::hasEditor() const {
    return true;
}

juce::AudioProcessorEditor* IntravenousAudioProcessor::createEditor() {
    return new IntravenousAudioProcessorEditor(*this, _value_tree_state);
}

void IntravenousAudioProcessor::getStateInformation(juce::MemoryBlock& write_buffer) {
    auto state = _value_tree_state.copyState();
    std::unique_ptr<juce::XmlElement> xml_state(state.createXml());
    copyXmlToBinary(*xml_state, write_buffer);
}

void IntravenousAudioProcessor::setStateInformation(const void* data, int size_in_bytes) {
    auto xml_state = getXmlFromBinary(data, size_in_bytes);
    if (xml_state && xml_state->hasTagName(_value_tree_state.state.getType()))
        _value_tree_state.replaceState(juce::ValueTree::fromXml(*xml_state));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new IntravenousAudioProcessor();
}
