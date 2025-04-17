#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NodeGraph.h"

juce::String const IntravenousAudioProcessor::WARP_THRESHOLD_ID = "warp_threshold";
juce::String const IntravenousAudioProcessor::NOISE_LEVEL_ID = "noise_level";

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
        }
    },
    _warp_threshold(_value_tree_state.getRawParameterValue(WARP_THRESHOLD_ID)),
    _noise_level(_value_tree_state.getRawParameterValue(NOISE_LEVEL_ID))
{
    init_graph();
}

class SampleRateNode : public iv::Node {
    iv::OutputPort _out_sample_rate;
    juce::AudioProcessor const* _audio_processor;

public:
    explicit SampleRateNode(juce::AudioProcessor const* audio_processor) noexcept :
        _audio_processor(audio_processor)
    {}

    void tick(std::span<iv::MidiMessage const> const& midi) noexcept override {
        _out_sample_rate.push(_audio_processor->getSampleRate());
    }

    std::span<iv::OutputPort const> outputs_impl() const noexcept override { return { &_out_sample_rate, 1 }; }
};

template<>
class iv::NodeFactory<SampleRateNode> : public iv::NodeFactoryBaseTemplate<SampleRateNode> {
    juce::AudioProcessor const* _audio_processor;

public:
    explicit NodeFactory(juce::AudioProcessor const* audio_processor = nullptr) noexcept :
        _audio_processor(audio_processor)
    {}

    std::unique_ptr<SampleRateNode> create_t() const {
        return std::make_unique<SampleRateNode>(_audio_processor);
    }

    std::unique_ptr<NodeFactoryBase> clone() const override {
        return std::make_unique<NodeFactory>(_audio_processor);
    }

    void set_audio_processor(juce::AudioProcessor const* audio_processor) {
        _audio_processor = audio_processor;
    }
};

template<class T>
class KnobNode : public iv::Node {
    iv::OutputPort _output;
    std::atomic<T>* _value;

public:
    explicit KnobNode(std::atomic<T>* value) noexcept: _value(value) {}

    void tick(std::span<iv::MidiMessage const> const&) noexcept override {
        _output.push(_value->load());
    }

    std::span<iv::OutputPort const> outputs_impl() const noexcept override { return { &_output, 1 }; }
};

template<class T>
class iv::NodeFactory<KnobNode<T>> : public iv::NodeFactoryBaseTemplate<KnobNode<T>> {
    std::atomic<T>* _value;

public:
    explicit NodeFactory(std::atomic<T>* value = nullptr) noexcept: _value(value) {}

    std::unique_ptr<KnobNode<T>> create_t() const {
        return std::make_unique<KnobNode<T>>(_value);
    }

    std::unique_ptr<NodeFactoryBase> clone() const override {
        return std::make_unique<NodeFactory>(_value);
    }

    void set_value(std::atomic<T>* value) {
        _value = value;
    }
};

void IntravenousAudioProcessor::init_graph() {
    auto graph_id = iv::NodeFactory<iv::Graph>::GRAPH_ID;
    iv::NodeFactory<iv::Graph> graph;
    _left_port =  graph.add_output_port();
    _right_port = graph.add_output_port();

    auto [midi_left, midi_left_id] = graph.add_node<iv::MidiNode>();
    {
        auto midi_graph_id = iv::NodeFactory<iv::Graph>::GRAPH_ID;
        auto& midi_graph = midi_left->get_voice_factory();
        auto [integrator, integrator_id] = midi_graph.add_node<iv::IntegratorNode>();
        auto [warper, warper_id] = midi_graph.add_node<iv::WarperNode>();
        auto [noise_generator, noise_generator_id] = midi_graph.add_node<iv::UniformNoiseNode>();

        auto [sample_rate, sample_rate_id] = midi_graph.add_node<SampleRateNode>(this);
        auto [warp_threshold_knob, warp_threshold_knob_id] = midi_graph.add_node<KnobNode<float>>(_warp_threshold);
        auto [noise_level_knob, noise_level_knob_id] = midi_graph.add_node<KnobNode<float>>(_noise_level);

        // main loop
        auto [frequency, frequency_id] = midi_graph.add_node<iv::MultiplyNode>(2);
        auto [frequency_offset, frequency_offset_id] = midi_graph.add_node<iv::ConstantNode>(4.0);
        midi_graph.connect({ midi_graph_id, midi_left->get_frequency_port() }, { frequency_id, 0 });
        midi_graph.connect({ frequency_offset_id, 0 }, { frequency_id, 1 });
        midi_graph.connect({ frequency_id, 0 }, { integrator_id, 0 });
        midi_graph.connect({ integrator_id, 0 }, { warper_id, 0 });
        midi_graph.connect({ warper_id, 1 }, { integrator_id, 1 });

        // noise
        auto [noise, noise_id] = midi_graph.add_node<iv::MultiplyNode>(2);
        midi_graph.connect({ noise_generator_id, 0 },  { noise_id, 0 });
        midi_graph.connect({ noise_level_knob_id, 0 }, { noise_id, 1 });

        // knobs
        midi_graph.connect({ sample_rate_id, 0 }, { integrator_id, 2 });
        midi_graph.connect({ warp_threshold_knob_id, 0 }, { warper_id, 1 });
        midi_graph.connect({ noise_id, 0 }, { warper_id, 0 });

        // out
        auto [amplitude, amplitude_id] = midi_graph.add_node<iv::MultiplyNode>(2);
        midi_graph.connect({ midi_graph_id, midi_left->get_amplitude_port() }, { amplitude_id, 0 });
        midi_graph.connect({ warper_id, 0 },                                   { amplitude_id, 1 });
        midi_graph.connect({ amplitude_id, 0 }, { midi_graph_id, midi_left->get_output_port() });
    }

    auto [midi_right, midi_right_id] = graph.add_node<iv::MidiNode>();
    *midi_right = *midi_left;

    graph.connect({ midi_left_id, 0 }, { graph_id, _left_port });
    graph.connect({ midi_right_id, 0 }, { graph_id, _right_port });

    _graph = graph.create_t();
    setLatencySamples(_graph->inner_latency());
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
    _midi_buffers.resize(samples_per_block);
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

    for (auto& midi_buffer : _midi_buffers) {
        for (size_t i = 0; i < midi_buffer.size(); ++i) {
            if (midi_buffer[i].type == iv::MidiMessageType::NONE) break;
            midi_buffer[i] = {};
        }
    }

    for (auto const& midi: midi_data) {
        auto const& midi_message = midi.getMessage();
        auto& midi_buffer = _midi_buffers[midi_message.getTimeStamp() + latency];
        for (size_t i = 0; i < midi_buffer.size(); ++i) {
            if (midi_buffer[i].type != iv::MidiMessageType::NONE) continue;
            if (midi_message.isNoteOn() && midi_message.getVelocity() > 0) {
                iv::MidiMessage iv_midi_message { .type = iv::MidiMessageType::NOTE_ON };
                iv_midi_message.note_on.amplitude = midi_message.getVelocity();
                iv_midi_message.note_on.note_number = midi_message.getNoteNumber();
                midi_buffer[i] = iv_midi_message;
                break;
            }
            else if (midi_message.isNoteOff() || midi_message.isNoteOn() && midi_message.getVelocity() == 0) {
                iv::MidiMessage iv_midi_message { .type = iv::MidiMessageType::NOTE_OFF };
                iv_midi_message.note_on.amplitude = midi_message.getVelocity();
                iv_midi_message.note_on.note_number = midi_message.getNoteNumber();
                midi_buffer[i] = iv_midi_message;
                break;
            }
        }
    }

    for (size_t sample_index = 0; sample_index < num_samples; ++sample_index) {
        auto& midi_buffer = _midi_buffers[sample_index];
        size_t buffer_size = 0;
        for (; buffer_size < midi_buffer.size(); ++buffer_size)
            if (midi_buffer[buffer_size].type == iv::MidiMessageType::NONE) break;

        _graph->tick({ midi_buffer.data(), buffer_size});
        for (size_t channel = 0; channel < channels; ++channel) {
            auto& output = _graph->outputs()[channel];
            write_buffer[channel][sample_index] = output.get();
        }
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
