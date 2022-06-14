#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::String const IntravenousAudioProcessor::DRY_GAIN_IDENTIFIER = "dry_gain";
juce::String const IntravenousAudioProcessor::WET_GAIN_IDENTIFIER = "wet_gain";
juce::String const IntravenousAudioProcessor::INPUT_GAIN_IDENTIFIER = "input_gain";
juce::String const IntravenousAudioProcessor::INPUT_OFFSET_IDENTIFIER = "input_offset";
juce::String const IntravenousAudioProcessor::WARP_THRESHOLD_IDENTIFIER = "warp_threshold";
juce::String const IntravenousAudioProcessor::WARP_SCALE_IDENTIFIER = "warp_scale";
juce::String const IntravenousAudioProcessor::WARP_DESTINATION_IDENTIFIER = "warp_destination";

IntravenousAudioProcessor::IntravenousAudioProcessor():
#ifndef JucePlugin_PreferredChannelConfigurations
    AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
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
                1.f),
            std::make_unique<juce::AudioParameterFloat>(
                INPUT_OFFSET_IDENTIFIER,
                "Input Offset",
                juce::NormalisableRange<float>(-1.f, 1.f, .0001f, .25f, true),
                0.f),
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
    _warp_threshold(_value_tree_state.getRawParameterValue(WARP_THRESHOLD_IDENTIFIER)),
    _warp_scale(_value_tree_state.getRawParameterValue(WARP_SCALE_IDENTIFIER)),
    _warp_destination(_value_tree_state.getRawParameterValue(WARP_DESTINATION_IDENTIFIER))
{
}

IntravenousAudioProcessor::~IntravenousAudioProcessor()
{
}

const juce::String IntravenousAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool IntravenousAudioProcessor::acceptsMidi() const
{
    return JucePlugin_WantsMidiInput;
}

bool IntravenousAudioProcessor::producesMidi() const
{
    return JucePlugin_ProducesMidiOutput;
}

bool IntravenousAudioProcessor::isMidiEffect() const
{
    return JucePlugin_IsMidiEffect;
}

double IntravenousAudioProcessor::getTailLengthSeconds() const
{
    return 0.;
}

int IntravenousAudioProcessor::getNumPrograms()
{
    return 1;
}

int IntravenousAudioProcessor::getCurrentProgram()
{
    return 0;
}

void IntravenousAudioProcessor::setCurrentProgram(int index)
{}

const juce::String IntravenousAudioProcessor::getProgramName(int index)
{
    return {};
}

void IntravenousAudioProcessor::changeProgramName(int index, const juce::String& newName)
{}

void IntravenousAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    clearSideEffects();
}

void IntravenousAudioProcessor::releaseResources()
{}

void IntravenousAudioProcessor::clearSideEffects()
{
    size_t const input_channels = getTotalNumInputChannels();
    auto const reset_samples = [=](auto& samples) {
        using Samples = std::remove_reference_t<decltype(samples)>;
        Samples tmp(input_channels, 0.f);
        samples.swap(tmp);
    };

    reset_samples(_output);
    reset_samples(_low_passed_output);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool IntravenousAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void IntravenousAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi_messages)
{
    juce::ScopedNoDenormals no_denormals;
    auto input_channels = getTotalNumInputChannels();
    auto output_channels = getTotalNumOutputChannels();

    for (int channel = 0; channel < input_channels; ++channel)
    {
        float* const channel_data = buffer.getWritePointer(channel);

        for (int sample_index = 0; sample_index < buffer.getNumSamples(); ++sample_index)
        {
            float const input_sample = channel_data[sample_index];
            float& output_sample = _output[channel];

            output_sample = output_sample + input_sample * _input_gain->load() + _input_offset->load();
            output_sample = warp_sample(output_sample);

            float const centered_sample = recenter_waveform(output_sample, _low_passed_output[channel]);
            channel_data[sample_index] = input_sample * _dry_gain->load() + centered_sample * _wet_gain->load();
        }
    }
}

void IntravenousAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{}

float IntravenousAudioProcessor::recenter_waveform(float const dry_sample, float& last_low_passed_sample) const
{
    double const cutoff_ratio = std::exp(-20. / getSampleRate());
    last_low_passed_sample = static_cast<float>(dry_sample * (1. - cutoff_ratio) + last_low_passed_sample * cutoff_ratio);
    return dry_sample - last_low_passed_sample;
}

float IntravenousAudioProcessor::warp_sample(float const dry_sample) const
{
    float const warp_threshold = _warp_threshold->load();
    
    if (dry_sample > warp_threshold)
        return warp_positive_sample(dry_sample, warp_threshold);
    else if (dry_sample < -warp_threshold)
        return -warp_positive_sample(-dry_sample, warp_threshold);
    else
        return dry_sample;
}

float IntravenousAudioProcessor::warp_positive_sample(float const sample, float const& warp_threshold) const
{
    float const warped_sample = std::fmodf((sample - warp_threshold) * _warp_scale->load(), 2.f);
    return warped_sample + _warp_destination->load();
};

bool IntravenousAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* IntravenousAudioProcessor::createEditor()
{
    return new IntravenousAudioProcessorEditor (*this, _value_tree_state);
}

void IntravenousAudioProcessor::getStateInformation (juce::MemoryBlock& write_buffer)
{
    auto state = _value_tree_state.copyState();
    std::unique_ptr<juce::XmlElement> xml_state(state.createXml());
    copyXmlToBinary(*xml_state, write_buffer);
}

void IntravenousAudioProcessor::setStateInformation (const void* data, int size_in_bytes)
{
    auto xml_state = getXmlFromBinary(data, size_in_bytes);
    if (xml_state.get() != nullptr && xml_state->hasTagName(_value_tree_state.state.getType()))
        _value_tree_state.replaceState(juce::ValueTree::fromXml(*xml_state));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IntravenousAudioProcessor();
}
