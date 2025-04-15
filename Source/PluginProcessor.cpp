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
{
    setLatencySamples(1);
    _unordered_midi[1000] = {
        juce::MidiMessage::noteOn(1, 70, juce::uint8(50))
    };
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

    reset_samples(_voices);
    reset_samples(_low_passed_voices);
    reset_samples(_latency_buffers);
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

float polyblep_phi(float const& sample, float const& warp_threshold) {
    return (sample / warp_threshold + 1.f) / 2.f;
}

float polyblep_p(float const& phi, float const& delta, float const& warp_threshold, bool pre_clip) {
    if (!pre_clip && phi < delta) {
        auto const& first_order = 2.f * phi / delta;
        auto const& second_order = phi / delta;
        return (first_order - second_order*second_order - 1) * warp_threshold;
    }
    if (pre_clip && 1 - delta <= phi) {
        auto const& second_order = (phi - 1) / delta + 1;
        return second_order*second_order * warp_threshold;
    }
    return 0;
}

void IntravenousAudioProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi_data) {
    juce::ScopedNoDenormals no_denormals;
    float* const* const write_buffer = audio.getArrayOfWritePointers();
    auto const num_samples = audio.getNumSamples();
    auto const channels = getTotalNumOutputChannels();
    auto const latency = getLatencySamples();

    for (auto const& midi: midi_data) {
        auto const& midi_message = midi.getMessage();
        if ((midi_message.isNoteOn() || midi_message.isNoteOff())) {
            _unordered_midi[midi_message.getTimeStamp() + latency].emplace_back(midi_message);
        }
    }

    for (size_t sample_index = 0; sample_index < num_samples + latency; ++sample_index) {
        auto const& midi_messages = _unordered_midi.find(sample_index);
        if (midi_messages != _unordered_midi.end()) {
            for (auto const& midi_message: midi_messages->second) {
                std::tuple midi_message_idx { midi_message.getNoteNumber(), midi_message.getChannel() };
                if (midi_message.isNoteOn() && midi_message.getVelocity() > 0) {
                    _note_velocities[midi_message_idx].push_back(midi_message.getVelocity());
                }
                else if (midi_message.isNoteOff() || midi_message.isNoteOn() && midi_message.getVelocity() == 0) {
                    auto& velocities = _note_velocities[midi_message_idx];
                    velocities.pop_back();
                    if (velocities.empty()) {
                        _note_velocities.erase(midi_message_idx);
                        for (auto& voices: _voices) {
                            voices.erase(midi_message_idx);
                        }
                        for (auto& low_passed_voices: _low_passed_voices) {
                            low_passed_voices.erase(midi_message_idx);
                        }
                    }
                }
            }
            _unordered_midi.erase(midi_messages->first);
        }

        float const dc_offset_gain = _dc_offset_gain->load();
        float const warp_threshold = _warp_threshold->load();

        if (sample_index < latency) {
            for (size_t channel = 0; channel < channels; ++channel) {
                auto& latency_buffer = _latency_buffers[channel];
                if (latency_buffer.empty()) {
                    write_buffer[channel][sample_index] = 0;
                }
                else {
                    write_buffer[channel][sample_index] = latency_buffer.front();
                    latency_buffer.pop();
                }
            }
        }
        else {
            for (size_t channel = 0; channel < channels; ++channel) {
                float correction = 0.f;
                float output = 0.f;
                for (auto const& [note_idx, velocities]: _note_velocities) {
                    auto const& [note_number, midi_channel] = note_idx;
                    float& voice = _voices[channel][note_idx];
                    float const voice_accumulated = accumulate_step(voice, warp_threshold, note_number);
                    auto const [did_warp, voice_warpped] = warp_sample(voice_accumulated, warp_threshold, note_number);

                    float const delta = (voice_accumulated - voice) / (2.f * warp_threshold);
                    float const phi_prev = polyblep_phi(voice, warp_threshold);
                    float const phi_next = polyblep_phi(voice_warpped, warp_threshold);

                    auto const& note_gain = velocities.back() / 127.f;
                    voice = voice_warpped;
                    output += voice_warpped * note_gain;

                    if (did_warp) {
                        correction -= polyblep_p(phi_prev, delta, warp_threshold, true) * note_gain;
                        output -= polyblep_p(phi_next, delta, warp_threshold, false) * note_gain;
                    }
                    //output += remove_dc_offset(voice_antialiased, dc_offset_gain, _low_passed_voices[channel][note_idx]);
                }
                write_buffer[channel][sample_index - 1] += correction;
                if (sample_index < num_samples) {
                    write_buffer[channel][sample_index] = output;
                }
                else {
                    _latency_buffers[channel].push(output);
                }
            }
        }
    }
}

void IntravenousAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) {
}

float IntravenousAudioProcessor::accumulate_step(float const& sample, float const& warp_threshold, int const& note_number) const {
    auto const& frequency = juce::MidiMessage::getMidiNoteInHertz(note_number);
    return sample + 2.f * warp_threshold * float(frequency / getSampleRate());
}

std::tuple<bool, float> IntravenousAudioProcessor::warp_sample(
    float dry_sample,
    float const& warp_threshold,
    int const& note_number
) const {
    if (dry_sample > warp_threshold) {
        return { true, warp_positive_sample(dry_sample, warp_threshold) };
    }
    else if (dry_sample < -warp_threshold) {
        return { true, -warp_positive_sample(-dry_sample, warp_threshold) };
    }
    else {
        return { false, dry_sample };
    }
}

float IntravenousAudioProcessor::warp_positive_sample(
    float const& dry_sample,
    float const& warp_threshold
) const {
    float const warped_sample = std::fmodf(dry_sample - warp_threshold, 2.f* warp_threshold) - warp_threshold;
    float const clipped_sample = std::min(std::max(warped_sample, -warp_threshold), warp_threshold);
    return clipped_sample;
}

float IntravenousAudioProcessor::remove_dc_offset(float const& dry_sample, float const& dc_offset_gain, float& last_low_passed_sample) const {
    float const cutoff_ratio = std::expf(-20.f / static_cast<float>(getSampleRate()));
    last_low_passed_sample = interpolate(dry_sample, last_low_passed_sample, cutoff_ratio);
    return dry_sample - last_low_passed_sample * dc_offset_gain;
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
