#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::String const IntravenousAudioProcessor::REMOVE_DC_OFFSET_IDENTIFIER = "remove_dc_offset";
juce::String const IntravenousAudioProcessor::WARP_THRESHOLD_IDENTIFIER = "warp_threshold";

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
            std::make_unique<juce::AudioParameterBool>(
                REMOVE_DC_OFFSET_IDENTIFIER,
                "Remove DC Offset",
                true),
            std::make_unique<juce::AudioParameterFloat>(
                WARP_THRESHOLD_IDENTIFIER,
                "Warp Threshold",
                juce::NormalisableRange<float>(0.f, 1.f, .0001f),
                1.f),
        }
    },
    _dc_offset_gain(_value_tree_state.getRawParameterValue(REMOVE_DC_OFFSET_IDENTIFIER)),
    _warp_threshold(_value_tree_state.getRawParameterValue(WARP_THRESHOLD_IDENTIFIER))
{}

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
    clearSideEffects();
}

void IntravenousAudioProcessor::releaseResources() {
}

void IntravenousAudioProcessor::clearSideEffects() {
    size_t const input_channels = getTotalNumOutputChannels();
    auto const reset_samples = [=](auto& samples) {
        using Samples = std::remove_reference_t<decltype(samples)>;
        Samples tmp(input_channels);
        samples.swap(tmp);
    };

    reset_samples(_last_outputs);
    reset_samples(_low_passed_last_outputs);
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

float interpolate(float const& min, float const& max, float const& ratio) {
    return min + (max - min) * ratio;
}

void IntravenousAudioProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi_data) {
    juce::ScopedNoDenormals no_denormals;
    int const channels = getTotalNumOutputChannels();

    float* const* const write_buffer = audio.getArrayOfWritePointers();

    std::unordered_map<size_t, std::vector<juce::MidiMessage>> unordered_midi;
    for (auto const& midi : midi_data) {
        auto const& midi_message = midi.getMessage();
        unordered_midi[midi_message.getTimeStamp()].emplace_back(midi_message);
    }

    for (size_t sample_index = 0; sample_index < audio.getNumSamples(); ++sample_index)
    {
        float const dc_offset_gain = _dc_offset_gain->load();
        float const warp_threshold = _warp_threshold->load();

        auto const& midi_messages = unordered_midi.find(sample_index);
        if (midi_messages != unordered_midi.end()) {
            for (auto const& midi_message: midi_messages->second) {
                auto const& midi_message_idx = [&]() {
                    return std::tuple { midi_message.getNoteNumber(), midi_message.getChannel() };
                };
                if (midi_message.getTimeStamp() == sample_index) {
                    if (midi_message.isNoteOn()) {
                        auto& velocity_data = _note_velocities[midi_message_idx()];
                        std::get<0>(velocity_data) += 1;
                        std::get<1>(velocity_data) += midi_message.getVelocity();
                    }
                    else if (midi_message.isNoteOff()) {
                        auto& velocity_data = _note_velocities[midi_message_idx()];
                        auto const& new_count = (std::get<0>(velocity_data) -= 1);
                        std::get<1>(velocity_data) -= midi_message.getVelocity();
                        if (new_count == 0) {
                            _note_velocities.erase(midi_message_idx());
                            for (auto& last_outputs: _last_outputs) {
                                last_outputs.erase(midi_message_idx());
                            }
                            for (auto& low_passed_last_outputs: _low_passed_last_outputs) {
                                low_passed_last_outputs.erase(midi_message_idx());
                            }
                        }
                    }
                }
            }
        }

        for (size_t channel = 0; channel < channels; ++channel) {
            float output = 0.f;

            for (auto const& [note_idx, velocity_data]: _note_velocities) {
                auto const& [note_number, channel] = note_idx;
                float last_output = _last_outputs[channel][note_idx];
                last_output = accumulate_step(last_output, warp_threshold, note_number);
                last_output = warp_sample(last_output, warp_threshold, note_number);
                _last_outputs[channel][note_idx] = last_output;

                auto const& [count, note_velocity] = velocity_data;
                output += remove_dc_offset(last_output, dc_offset_gain, _low_passed_last_outputs[channel][note_idx]) * (note_velocity / 127.f);
            }

            write_buffer[channel][sample_index] = output;
        }
    }
}

float IntravenousAudioProcessor::accumulate_step(float const& sample, float const& threshold, int const& note_number) const {
    auto const& frequency = juce::MidiMessage::getMidiNoteInHertz(note_number);
    return sample + 2.f * threshold * float(frequency / getSampleRate());
}

void IntravenousAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) {
}

float IntravenousAudioProcessor::remove_dc_offset(float const& dry_sample, float const& dc_offset_gain, float& last_low_passed_sample) const {
    float const cutoff_ratio = std::expf(-20.f / static_cast<float>(getSampleRate()));
    last_low_passed_sample = interpolate(dry_sample, last_low_passed_sample, cutoff_ratio);
    return dry_sample - last_low_passed_sample * dc_offset_gain;
}

float IntravenousAudioProcessor::warp_sample(
    float const& dry_sample,
    float const& warp_threshold,
    int const& note_number
) const {
    if (dry_sample > warp_threshold)
        return warp_positive_sample(dry_sample, warp_threshold, note_number);
    else if (dry_sample < -warp_threshold)
        return -warp_positive_sample(-dry_sample, warp_threshold, note_number);
    else
        return dry_sample;
}

float IntravenousAudioProcessor::warp_positive_sample(
    float const& dry_sample,
    float const& warp_threshold,
    int const& note_number
) const {
    float const skewed_sample = dry_sample - warp_threshold;
    float const warped_sample = std::fmodf(skewed_sample, 2.f*warp_threshold) - warp_threshold;
    float const clipped_sample = std::min(std::max(warped_sample , -warp_threshold), warp_threshold);
    return clipped_sample;
};

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
