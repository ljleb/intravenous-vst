#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::String const IntravenousAudioProcessor::DRY_GAIN_IDENTIFIER = "dry_gain";
juce::String const IntravenousAudioProcessor::WET_GAIN_IDENTIFIER = "wet_gain";
juce::String const IntravenousAudioProcessor::INPUT_GAIN_IDENTIFIER = "input_gain";
juce::String const IntravenousAudioProcessor::INPUT_OFFSET_IDENTIFIER = "input_offset";
juce::String const IntravenousAudioProcessor::INPUT_OFFSET_DECAY_IDENTIFIER = "input_offset_decay";
juce::String const IntravenousAudioProcessor::WARP_THRESHOLD_IDENTIFIER = "warp_threshold";
juce::String const IntravenousAudioProcessor::WARP_SCALE_IDENTIFIER = "warp_scale";
juce::String const IntravenousAudioProcessor::WARP_DESTINATION_IDENTIFIER = "warp_destination";

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
                DRY_GAIN_IDENTIFIER,
                "Dry Gain",
                juce::NormalisableRange<float>(0.f, 1.f, .01f),
                .5f),
            std::make_unique<juce::AudioParameterFloat>(
                WET_GAIN_IDENTIFIER,
                "Wet Gain",
                juce::NormalisableRange<float>(0.f, 1.f, .01f),
                .25f),
            std::make_unique<juce::AudioParameterFloat>(
                INPUT_GAIN_IDENTIFIER,
                "Input Gain",
                juce::NormalisableRange<float>(0.f, 10.f, .0001f, .25f),
                .25f),
            std::make_unique<juce::AudioParameterFloat>(
                INPUT_OFFSET_IDENTIFIER,
                "Input Offset",
                juce::NormalisableRange<float>(-1.f, 1.f, .0001f, .25f, true),
                0.f),
            std::make_unique<juce::AudioParameterInt>(
                INPUT_OFFSET_DECAY_IDENTIFIER,
                "Input Offset Decay",
                0, static_cast<int>(std::pow(2., 16.)),
                static_cast<int>(std::pow(2., 11.))),
            std::make_unique<juce::AudioParameterFloat>(
                WARP_THRESHOLD_IDENTIFIER,
                "Warp Threshold",
                juce::NormalisableRange<float>(0.f, 1.f, .01f),
                1.f),
            std::make_unique<juce::AudioParameterFloat>(
                WARP_SCALE_IDENTIFIER,
                "Warp Scale",
                juce::NormalisableRange<float>(0.f, 10.f, .01f),
                1.f),
            std::make_unique<juce::AudioParameterFloat>(
                WARP_DESTINATION_IDENTIFIER,
                "Warp Destination",
                juce::NormalisableRange<float>(-1.f, 1.f, .01f),
                -.25f),
        }
    },
    _dry_gain(_value_tree_state.getRawParameterValue(DRY_GAIN_IDENTIFIER)),
    _wet_gain(_value_tree_state.getRawParameterValue(WET_GAIN_IDENTIFIER)),
    _input_gain(_value_tree_state.getRawParameterValue(INPUT_GAIN_IDENTIFIER)),
    _input_offset(_value_tree_state.getRawParameterValue(INPUT_OFFSET_IDENTIFIER)),
    _input_offset_decay(_value_tree_state.getRawParameterValue(INPUT_OFFSET_DECAY_IDENTIFIER)),
    _warp_threshold(_value_tree_state.getRawParameterValue(WARP_THRESHOLD_IDENTIFIER)),
    _warp_scale(_value_tree_state.getRawParameterValue(WARP_SCALE_IDENTIFIER)),
    _warp_destination(_value_tree_state.getRawParameterValue(WARP_DESTINATION_IDENTIFIER))
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
    size_t const input_channels = getTotalNumInputChannels();
    auto const reset_samples = [=](auto& samples) {
        using Samples = std::remove_reference_t<decltype(samples)>;
        Samples tmp(input_channels, 0.f);
        samples.swap(tmp);
    };

    reset_samples(_output);
    reset_samples(_low_passed_output);
    _input_loudness = 0.f;
    _samples_since_input_loudness_update = 0;
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

void IntravenousAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals no_denormals;
    auto input_channels = getTotalNumInputChannels();
    auto output_channels = getTotalNumOutputChannels();

    float* const* const write_buffer = buffer.getArrayOfWritePointers();

    for (int sample_index = 0; sample_index < buffer.getNumSamples(); ++sample_index)
    {
        float const dry_gain = _dry_gain->load();
        float const wet_gain = _wet_gain->load();
        float const input_gain = _input_gain->load();
        float const input_offset = _input_offset->load();
        float const input_offset_decay = _input_offset_decay->load();
        float const warp_threshold = _warp_threshold->load();
        float const warp_scale = _warp_scale->load();
        float const warp_destination = _warp_destination->load();

        for (int channel = 0; channel < input_channels; ++channel)
        {
            float& input_sample = write_buffer[channel][sample_index];
            float& output_sample = _output[channel];

            update_input_loudness(input_sample, input_offset_decay);
            output_sample += input_sample * input_gain + input_offset * _input_loudness;
            output_sample = warp_sample(output_sample, warp_threshold, warp_scale, warp_destination);

            float const centered_sample = recenter_waveform(output_sample, _low_passed_output[channel]);
            input_sample = input_sample * dry_gain + centered_sample * wet_gain;
        }
    }
}

void IntravenousAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) {
}

void IntravenousAudioProcessor::update_input_loudness(float const& dry_sample, float const& input_offset_decay) {
    if (std::abs(dry_sample) > _input_loudness || _samples_since_input_loudness_update >= input_offset_decay) {
        _input_loudness = std::abs(dry_sample);
        _samples_since_input_loudness_update = 0;
    }
    else
        ++_samples_since_input_loudness_update;
}

float IntravenousAudioProcessor::recenter_waveform(float const dry_sample, float& last_low_passed_sample) const {
    double const cutoff_ratio = std::exp(-20. / getSampleRate());
    last_low_passed_sample = static_cast<float>(dry_sample * (1. - cutoff_ratio) + last_low_passed_sample * cutoff_ratio);
    return dry_sample - last_low_passed_sample;
}

float IntravenousAudioProcessor::warp_sample(
    float const& dry_sample,
    float const& warp_threshold,
    float const& warp_scale,
    float const& warp_destination
) const {
    if (dry_sample > warp_threshold)
        return warp_positive_sample(dry_sample, warp_threshold, warp_scale, warp_destination);
    else if (dry_sample < -warp_threshold)
        return -warp_positive_sample(-dry_sample, warp_threshold, warp_scale, warp_destination);
    else
        return dry_sample;
}

float IntravenousAudioProcessor::warp_positive_sample(
    float const& dry_sample,
    float const& warp_threshold,
    float const& warp_scale,
    float const& warp_destination
) const {
    float const skewed_sample = (dry_sample - warp_threshold) * warp_scale;
    float const warped_sample = std::fmodf(skewed_sample, 2.f) + warp_destination;
    return warped_sample;
};

bool IntravenousAudioProcessor::hasEditor() const {
    return true;
}

juce::AudioProcessorEditor* IntravenousAudioProcessor::createEditor() {
    return new IntravenousAudioProcessorEditor(*this, _value_tree_state);
}

void IntravenousAudioProcessor::getStateInformation (juce::MemoryBlock& write_buffer) {
    auto state = _value_tree_state.copyState();
    std::unique_ptr<juce::XmlElement> xml_state(state.createXml());
    copyXmlToBinary(*xml_state, write_buffer);
}

void IntravenousAudioProcessor::setStateInformation (const void* data, int size_in_bytes) {
    auto xml_state = getXmlFromBinary(data, size_in_bytes);
    if (xml_state && xml_state->hasTagName(_value_tree_state.state.getType()))
        _value_tree_state.replaceState(juce::ValueTree::fromXml(*xml_state));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new IntravenousAudioProcessor();
}
