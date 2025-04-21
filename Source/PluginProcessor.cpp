#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Intravenous_NodeGraph/public.h"


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
                juce::NormalisableRange<float>(0.f, 1.f, .0001f),
                0.f),
        }
    },
    _warp_threshold(_value_tree_state.getRawParameterValue(WARP_THRESHOLD_ID)),
    _noise_level(_value_tree_state.getRawParameterValue(NOISE_LEVEL_ID)),
    _iir_outw0(_value_tree_state.getRawParameterValue(IIR_OUTW0_ID))
{
    setLatencySamples(iv::init_graph(&_update_frequency, _channels, _warp_threshold, _noise_level, _iir_outw0));
}

IntravenousAudioProcessor::~IntravenousAudioProcessor() {
    iv::free_graph();
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
    _update_frequency = 1.0 / sample_rate;
    _midi_buffers.resize(samples_per_block);
    _midi_buffer_sizes.resize(samples_per_block, 0);
    _midi_buffers.shrink_to_fit();
    setLatencySamples(iv::init_graph(&_update_frequency, _channels, _warp_threshold, _noise_level, _iir_outw0));
}

void IntravenousAudioProcessor::releaseResources() {
    iv::free_graph();
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
        iv::tick({ _midi_buffers[sample].data(), _midi_buffer_sizes[sample] });
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
