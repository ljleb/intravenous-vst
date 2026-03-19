#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Intravenous_NodeGraph/public.h"
#include <chrono>


juce::String const IntravenousAudioProcessor::UNIFORM_NOISE_LEVEL_ID = "noise_level";
juce::String const IntravenousAudioProcessor::TIME_WINDOW_ID = "time_window";
juce::String const IntravenousAudioProcessor::TIME_OFFSET_ID = "time_offset";
juce::String const IntravenousAudioProcessor::GAUSSIAN_NOISE_RATIO_ID = "gaussian_noise_ratio";
juce::String const IntravenousAudioProcessor::HARMONICS_NOISE_RATIO_ID = "harmonics_noise_ratio";
juce::String const IntravenousAudioProcessor::NOISE_LOW_PASS_ID = "noise_low_pass";
juce::String const IntravenousAudioProcessor::NOISE_HIGH_PASS_ID = "noise_high_pass";

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
                UNIFORM_NOISE_LEVEL_ID,
                "Noise Level",
                juce::NormalisableRange<float>(0.f, 1.0f, .0001f),
                0.f),
            std::make_unique<juce::AudioParameterInt>(
                TIME_WINDOW_ID,
                "Time Window",
                0, 2048,
                0),
            std::make_unique<juce::AudioParameterInt>(
                TIME_OFFSET_ID,
                "Time Offset",
                0, 48000,
                0),
            std::make_unique<juce::AudioParameterFloat>(
                GAUSSIAN_NOISE_RATIO_ID,
                "Tail Heaviness",
                juce::NormalisableRange<float>(0.f, 1.0f, .0001f),
                0.f),
            std::make_unique<juce::AudioParameterFloat>(
                HARMONICS_NOISE_RATIO_ID,
                "Harmonics Noise Ratio",
                juce::NormalisableRange<float>(0.f, 1.0f, .0001f),
                0.f),
            std::make_unique<juce::AudioParameterFloat>(
                NOISE_LOW_PASS_ID,
                "Noise Low Pass",
                juce::NormalisableRange<float>(0.f, 1.f, .0001f),
                0.f),
            std::make_unique<juce::AudioParameterFloat>(
                NOISE_HIGH_PASS_ID,
                "Noise High Pass",
                juce::NormalisableRange<float>(0.f, 1.f, .0001f),
                1.f),
        }
    },
    _uniform_noise_level(_value_tree_state.getRawParameterValue(UNIFORM_NOISE_LEVEL_ID)),
    _time_offset(_value_tree_state.getRawParameterValue(TIME_OFFSET_ID)),
    _time_window(_value_tree_state.getRawParameterValue(TIME_WINDOW_ID)),
    _gaussian_noise_ratio(_value_tree_state.getRawParameterValue(GAUSSIAN_NOISE_RATIO_ID)),
    _harmonics_noise_ratio(_value_tree_state.getRawParameterValue(HARMONICS_NOISE_RATIO_ID)),
    _noise_low_pass(_value_tree_state.getRawParameterValue(NOISE_LOW_PASS_ID)),
    _noise_high_pass(_value_tree_state.getRawParameterValue(NOISE_HIGH_PASS_ID)),
    _song_index(0)
{
    try {
        _node_processor = iv::init_graph(
            &_update_frequency,
            _channels,
            _uniform_noise_level,
            _gaussian_noise_ratio,
            _harmonics_noise_ratio,
            _noise_low_pass,
            _noise_high_pass);
        setLatencySamples(_node_processor->get_latency());
    }
    catch (std::exception const& e) {
        juce::Logger::outputDebugString(e.what());
        throw;
    }
}

IntravenousAudioProcessor::~IntravenousAudioProcessor() {
    if (_node_processor) iv::free_graph(_node_processor);
    _node_processor = nullptr;
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
    if (_node_processor) iv::free_graph(_node_processor);
    _update_frequency = 1.0 / sample_rate;
    _midi_buffers.resize(samples_per_block);
    _midi_buffer_sizes.resize(samples_per_block, 0);
    _midi_buffers.shrink_to_fit();
    _node_processor = iv::init_graph(
        &_update_frequency,
        _channels,
        _uniform_noise_level,
        _gaussian_noise_ratio,
        _harmonics_noise_ratio,
        _noise_low_pass,
        _noise_high_pass);
    setLatencySamples(_node_processor->get_latency());
}

void IntravenousAudioProcessor::releaseResources() {
    if (_node_processor) iv::free_graph(_node_processor);
    _node_processor = nullptr;
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

#define JUCE_DISABLE_ASSERTIONS 0
void IntravenousAudioProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi_data) {
    juce::ScopedNoDenormals no_denormals;

    auto start = std::chrono::system_clock::now();
    float* const* const write_buffer = audio.getArrayOfWritePointers();
    auto const num_samples = audio.getNumSamples();
    auto const channels = getTotalNumOutputChannels();
    auto const latency = getLatencySamples();

    for (auto const& midi: midi_data) {
        auto const& midi_message = midi.getMessage();
        size_t sample_idx = size_t(midi_message.getTimeStamp());
        auto& midi_buffer = _midi_buffers[sample_idx];
        auto& buffer_size = _midi_buffer_sizes[sample_idx];
        if (buffer_size >= midi_buffer.size()) continue;  // drop message, no space left
        if (midi_message.isNoteOn() && midi_message.getVelocity() > 0) {
            iv::MidiMessage iv_midi_message { .type = iv::MidiMessageType::NOTE_ON };
            iv_midi_message.note_on.amplitude = midi_message.getVelocity();
            iv_midi_message.note_on.note_number = uint8_t(midi_message.getNoteNumber());
            iv_midi_message.note_on.channel = uint8_t(midi_message.getChannel() - 1);
            midi_buffer[buffer_size++] = iv_midi_message;
        }
        else if (midi_message.isNoteOff() || midi_message.isNoteOn() && midi_message.getVelocity() == 0) {
            iv::MidiMessage iv_midi_message { .type = iv::MidiMessageType::NOTE_OFF };
            iv_midi_message.note_off.note_number = uint8_t(midi_message.getNoteNumber());
            iv_midi_message.note_off.channel = uint8_t(midi_message.getChannel() - 1);
            midi_buffer[buffer_size++] = iv_midi_message;
        }
        else if (midi_message.isPitchWheel()) {
            iv::MidiMessage iv_midi_message { .type = iv::MidiMessageType::PITCH_WHEEL };
            iv_midi_message.pitch_wheel.pitch_value = uint16_t(midi_message.getPitchWheelValue());
            iv_midi_message.pitch_wheel.channel = uint8_t(midi_message.getChannel() - 1);
            midi_buffer[buffer_size++] = iv_midi_message;
        }
    }

    auto const position_info = this->getPlayHead()->getPosition();
    bool has_play_head = position_info.hasValue() && position_info->getTimeInSamples().hasValue();
    if (has_play_head)
    {
        _song_index = *position_info->getTimeInSamples() + size_t(_time_offset->load(std::memory_order_relaxed));
    }

    // move pointers to the start of the buffer
    for (size_t channel = 0; channel < channels; ++channel) {
        _channels[channel] = write_buffer[channel];
    }

    for (size_t sample = 0; sample < num_samples; ++sample) {
        iv::tick(_node_processor, { _midi_buffers[sample].data(), _midi_buffer_sizes[sample] }, _song_index);
        for (size_t channel = 0; channel < channels; ++channel) {
            ++_channels[channel];
        }
        _midi_buffer_sizes[sample] = 0;
        size_t time_window = _time_window->load(std::memory_order_relaxed);
        if (time_window)
        {
            _song_index = (_song_index + 1) % time_window;
        }
        else
        {
            ++_song_index;
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
