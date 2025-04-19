//#include "PluginProcessor.h"
//#include "PluginEditor.h"
//#include "NodeGraph.h"
//
//juce::String const IntravenousAudioProcessor::WARP_THRESHOLD_ID = "warp_threshold";
//juce::String const IntravenousAudioProcessor::NOISE_LEVEL_ID = "noise_level";
//juce::String const IntravenousAudioProcessor::IIR_OUTW0_ID = "iir_outw0";
//
//IntravenousAudioProcessor::IntravenousAudioProcessor():
//    #ifndef JucePlugin_PreferredChannelConfigurations
//        AudioProcessor(BusesProperties()
//            #if ! JucePlugin_IsMidiEffect
//                #if ! JucePlugin_IsSynth
//                    .withInput("Input", juce::AudioChannelSet::stereo(), true)
//                #endif
//                .withOutput("Output", juce::AudioChannelSet::stereo(), true)
//            #endif
//        ),
//    #endif
//    _value_tree_state {
//        *this, nullptr, "PARAMETERS", {
//            std::make_unique<juce::AudioParameterFloat>(
//                WARP_THRESHOLD_ID,
//                "Warp Threshold",
//                juce::NormalisableRange<float>(0.f, 1.f, .0001f),
//                1.f),
//            std::make_unique<juce::AudioParameterFloat>(
//                NOISE_LEVEL_ID,
//                "Noise Level",
//                juce::NormalisableRange<float>(0.f, 0.1f, .0001f),
//                0.f),
//            std::make_unique<juce::AudioParameterFloat>(
//                IIR_OUTW0_ID,
//                "IIR Out Weight 0",
//                juce::NormalisableRange<float>(-1.f, 1.f, .0001f),
//                0.f),
//        }
//    },
//    _warp_threshold(_value_tree_state.getRawParameterValue(WARP_THRESHOLD_ID)),
//    _noise_level(_value_tree_state.getRawParameterValue(NOISE_LEVEL_ID)),
//    _iir_outw0(_value_tree_state.getRawParameterValue(IIR_OUTW0_ID)),
//    _node_processor(init_graph())
//{}
//
//class SampleRateNode {
//    juce::AudioProcessor const* _audio_processor;
//
//public:
//    explicit constexpr SampleRateNode(juce::AudioProcessor const* audio_processor) noexcept :
//        _audio_processor(audio_processor)
//    {}
//
//    void tick(iv::TickState const& state) noexcept {
//        auto& out_sample_rate = state.outputs[0];
//        out_sample_rate.push(_audio_processor->getSampleRate());
//    }
//
//    constexpr auto outputs() const noexcept
//    {
//        return std::array<iv::OutputConfig, 1>{};
//    }
//};
//
//template<class T>
//class KnobNode {
//    iv::OutputPort _output;
//    std::atomic<T>* _value;
//
//public:
//    explicit KnobNode(std::atomic<T>* value) noexcept :
//        _value(value)
//    {}
//
//    void tick(iv::TickState const& state) noexcept {
//        auto& out = state.outputs[0];
//        out.push(_value->load());
//    }
//
//    constexpr auto outputs() const noexcept
//    {
//        return std::array<iv::OutputConfig, 1>{};
//    }
//};
//
//auto IntravenousAudioProcessor::init_graph() {
//    using GraphNode = iv::GraphNode<>;
//    auto graph_id = GraphNode::GRAPH_ID;
//    std::vector<iv::DynamicNode> nodes;
//    std::vector<iv::GraphEdge> edges;
//
//    size_t num_outputs = 2;
//
//    auto sample_rate = SampleRateNode(this);
//    auto warp_threshold_knob = KnobNode<float>(_warp_threshold);
//    auto iir_outw0_knob = KnobNode<float>(_iir_outw0);
//
//    // noise
//    auto noise = iv::GraphFactory<KnobNode<float>>(_noise_level) * iv::GraphFactory<iv::UniformNoiseNode>();
//
//    // shared inputs
//    graph.connect({ noise_id, 0 }, { midi_id, midi_noise_generator_port });
//    graph.connect({ sample_rate_id, 0 }, { midi_id, midi_sample_rate_port });
//    graph.connect({ warp_threshold_knob_id, 0 }, { midi_id, midi_warp_threshold_port });
//    graph.connect({ iir_outw0_knob_id, 0 }, { midi_id, midi_iir_outw0_port });
//
//    // midi voice
//    {
//        auto voice_id = iv::NodeFactory<iv::Graph>::GRAPH_ID;
//        auto& voice = midi->get_voice_factory();
//        auto [integrator, integrator_id] = voice.add_node<iv::IntegratorNode>();
//        auto [iir, iir_id] = voice.add_node<iv::IirFilterNode>();
//        auto [warper, warper_id] = voice.add_node<iv::WarperNode>();
//
//        auto [frequency, frequency_id] = voice.add_node<iv::MultiplyNode>(2);
//        auto [frequency_offset, frequency_offset_id] = voice.add_node<iv::ConstantNode>(4.0);
//        voice.connect({ voice_id, midi->get_voice_frequency_port() }, { frequency_id, 0 });
//        voice.connect({ frequency_offset_id, 0 }, { frequency_id, 1 });
//
//        voice.connect({ voice_id, voice_iir_outw0_port }, { iir_id, 5 });
//
//        // main loop
//        voice.connect({ integrator_id, 0 }, { warper_id, 0 });
//        voice.connect({ warper_id, 1 }, { integrator_id, 1 });
//        //voice.connect({ integrator_id, 0 }, { iir_id, 0 }); voice.connect({ iir_id, 0 }, { warper_id, 0 });
//
//        // knobs
//        voice.connect({ frequency_id, 0 }, { integrator_id, 0 });
//        voice.connect({ voice_id, voice_sample_rate_port }, { integrator_id, 2 });
//        voice.connect({ voice_id, voice_noise_generator_port }, { warper_id, 0 });
//        voice.connect({ voice_id, voice_warp_threshold_port }, { warper_id, 1 });
//
//        // out
//        auto [amplitude, amplitude_id] = voice.add_node<iv::MultiplyNode>(2);
//        voice.connect({ voice_id, midi->get_voice_amplitude_port() }, { amplitude_id, 0 });
//        voice.connect({ warper_id, 0 }, { amplitude_id, 1 });
//        voice.connect({ amplitude_id, 0 }, { voice_id, midi->get_voice_output_port() });
//    }
//
//    auto channels = std::array { iv::MidiNode(voice_graph), iv::MidiNode(voice_graph) };
//
//    graph.connect({ midi_id, 0 }, { graph_id, _left_port });
//    graph.connect({ midi_right_id, 0 }, { graph_id, _right_port });
//
//    auto graph = GraphNode();
//    setLatencySamples(graph.inner_latency());
//    return graph;
//}
//
//IntravenousAudioProcessor::~IntravenousAudioProcessor() {
//}
//
//const juce::String IntravenousAudioProcessor::getName() const {
//    return JucePlugin_Name;
//}
//
//bool IntravenousAudioProcessor::acceptsMidi() const {
//    return JucePlugin_WantsMidiInput;
//}
//
//bool IntravenousAudioProcessor::producesMidi() const {
//    return JucePlugin_ProducesMidiOutput;
//}
//
//bool IntravenousAudioProcessor::isMidiEffect() const {
//    return JucePlugin_IsMidiEffect;
//}
//
//double IntravenousAudioProcessor::getTailLengthSeconds() const {
//    return 0.;
//}
//
//int IntravenousAudioProcessor::getNumPrograms() {
//    return 1;
//}
//
//int IntravenousAudioProcessor::getCurrentProgram() {
//    return 0;
//}
//
//void IntravenousAudioProcessor::setCurrentProgram(int index) {
//}
//
//const juce::String IntravenousAudioProcessor::getProgramName(int index) {
//    return {};
//}
//
//void IntravenousAudioProcessor::changeProgramName(int index, const juce::String& new_name) {
//}
//
//void IntravenousAudioProcessor::prepareToPlay(double sample_rate, int samples_per_block) {
//    _midi_buffers.resize(samples_per_block);
//}
//
//void IntravenousAudioProcessor::releaseResources() {
//}
//
//#ifndef JucePlugin_PreferredChannelConfigurations
//bool IntravenousAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
//    #if JucePlugin_IsMidiEffect
//        juce::ignoreUnused(layouts);
//        return true;
//    #else
//        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
//            layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
//            return false;
//
//        #if ! JucePlugin_IsSynth
//            if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
//                return false;
//        #endif
//
//        return true;
//    #endif
//}
//#endif
//
//void IntravenousAudioProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi_data) {
//    juce::ScopedNoDenormals no_denormals;
//    float* const* const write_buffer = audio.getArrayOfWritePointers();
//    auto const num_samples = audio.getNumSamples();
//    auto const channels = getTotalNumOutputChannels();
//    auto const latency = getLatencySamples();
//
//    for (auto& midi_buffer : _midi_buffers) {
//        for (size_t i = 0; i < midi_buffer.size(); ++i) {
//            if (midi_buffer[i].type == iv::MidiMessageType::NONE) break;
//            midi_buffer[i] = {};
//        }
//    }
//
//    for (auto const& midi: midi_data) {
//        auto const& midi_message = midi.getMessage();
//        auto& midi_buffer = _midi_buffers[midi_message.getTimeStamp() + latency];
//        for (size_t i = 0; i < midi_buffer.size(); ++i) {
//            if (midi_buffer[i].type != iv::MidiMessageType::NONE) continue;
//            if (midi_message.isNoteOn() && midi_message.getVelocity() > 0) {
//                iv::MidiMessage iv_midi_message { .type = iv::MidiMessageType::NOTE_ON };
//                iv_midi_message.note_on.amplitude = midi_message.getVelocity();
//                iv_midi_message.note_on.note_number = midi_message.getNoteNumber();
//                midi_buffer[i] = iv_midi_message;
//                break;
//            }
//            else if (midi_message.isNoteOff() || midi_message.isNoteOn() && midi_message.getVelocity() == 0) {
//                iv::MidiMessage iv_midi_message { .type = iv::MidiMessageType::NOTE_OFF };
//                iv_midi_message.note_off.note_number = midi_message.getNoteNumber();
//                midi_buffer[i] = iv_midi_message;
//                break;
//            }
//        }
//    }
//
//    for (size_t sample_index = 0; sample_index < num_samples; ++sample_index) {
//        auto& midi_buffer = _midi_buffers[sample_index];
//        size_t buffer_size = 0;
//        for (; buffer_size < midi_buffer.size(); ++buffer_size)
//            if (midi_buffer[buffer_size].type == iv::MidiMessageType::NONE) break;
//
//        _graph->tick({ midi_buffer.data(), buffer_size});
//        for (size_t channel = 0; channel < channels; ++channel) {
//            auto& output = _graph->outputs()[channel];
//            write_buffer[channel][sample_index] = output.get();
//        }
//    }
//}
//
//void IntravenousAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) {
//}
//
//bool IntravenousAudioProcessor::hasEditor() const {
//    return true;
//}
//
//juce::AudioProcessorEditor* IntravenousAudioProcessor::createEditor() {
//    return new IntravenousAudioProcessorEditor(*this, _value_tree_state);
//}
//
//void IntravenousAudioProcessor::getStateInformation(juce::MemoryBlock& write_buffer) {
//    auto state = _value_tree_state.copyState();
//    std::unique_ptr<juce::XmlElement> xml_state(state.createXml());
//    copyXmlToBinary(*xml_state, write_buffer);
//}
//
//void IntravenousAudioProcessor::setStateInformation(const void* data, int size_in_bytes) {
//    auto xml_state = getXmlFromBinary(data, size_in_bytes);
//    if (xml_state && xml_state->hasTagName(_value_tree_state.state.getType()))
//        _value_tree_state.replaceState(juce::ValueTree::fromXml(*xml_state));
//}
//
//juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
//    return new IntravenousAudioProcessor();
//}
