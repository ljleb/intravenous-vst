/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::String const IntravenousAudioProcessor::INPUT_GAIN_IDENTIFIER = "input_gain";
juce::String const IntravenousAudioProcessor::OUTPUT_GAIN_IDENTIFIER = "output_gain";
juce::String const IntravenousAudioProcessor::WARP_SCALE_IDENTIFIER = "warp_scale";

//==============================================================================
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
                INPUT_GAIN_IDENTIFIER,
                "Input Gain",
                juce::NormalisableRange<float>(0.f, 10.f),
                1.f),
            std::make_unique<juce::AudioParameterFloat>(
                OUTPUT_GAIN_IDENTIFIER,
                "Output Gain",
                juce::NormalisableRange<float>(0.f, 1.f),
                .5f),
            std::make_unique<juce::AudioParameterFloat>(
                WARP_SCALE_IDENTIFIER,
                "Warp Scale",
                juce::NormalisableRange<float>(0.f, 1.f),
                0.f),
        }
    }
{
}

IntravenousAudioProcessor::~IntravenousAudioProcessor()
{
}

//==============================================================================
const juce::String IntravenousAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool IntravenousAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool IntravenousAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool IntravenousAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double IntravenousAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int IntravenousAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int IntravenousAudioProcessor::getCurrentProgram()
{
    return 0;
}

void IntravenousAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String IntravenousAudioProcessor::getProgramName (int index)
{
    return {};
}

void IntravenousAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void IntravenousAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    _output.resize(getTotalNumInputChannels(), 0.0);
}

void IntravenousAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.

    //decltype(_output) temp;
    //_output.swap(temp);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool IntravenousAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void IntravenousAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        for (int sample_index = 0; sample_index < buffer.getNumSamples(); ++sample_index)
        {
            float const input_sample = buffer.getSample(channel, sample_index);
            float& output_sample = _output[channel];

            recenter_waveform(output_sample, input_sample);
            output_sample += input_sample * get_parameter(INPUT_GAIN_IDENTIFIER);
            warp_waveform(output_sample);
            channelData[sample_index] = output_sample * get_parameter(OUTPUT_GAIN_IDENTIFIER);
        }
    }
}

void IntravenousAudioProcessor::recenter_waveform(float& output_sample, float const input_sample) const
{
    if (input_sample == 0) output_sample *= 0.999f;
}

void IntravenousAudioProcessor::warp_waveform(float& output_sample) const
{
    auto const scaled_fmodf = [this](float const x) {
        float const wrapped_sample = std::fmodf(x + 1.f, 2.f);
        float const negative_sample = wrapped_sample - 2.f;
        float const scaled_sample = negative_sample * get_parameter(WARP_SCALE_IDENTIFIER);
        return scaled_sample + 1.f;
    };

    if (output_sample > 1.f)
        output_sample = scaled_fmodf(output_sample);
    else if (output_sample < -1.f)
        output_sample = -scaled_fmodf(-output_sample);
}

//==============================================================================
bool IntravenousAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* IntravenousAudioProcessor::createEditor()
{
    return new IntravenousAudioProcessorEditor (*this);
}

//==============================================================================
void IntravenousAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.

    juce::MemoryOutputStream stream(destData, true);

    stream.writeString(_value_tree_state.state.toXmlString());
}

void IntravenousAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.

    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);

    _value_tree_state.replaceState(juce::ValueTree::fromXml(stream.readString()));
}

juce::AudioProcessorValueTreeState& IntravenousAudioProcessor::getValueTreeState()
{
    return _value_tree_state;
}

float IntravenousAudioProcessor::get_parameter(juce::StringRef const parameter_identifier) const
{
    return _value_tree_state.getRawParameterValue(parameter_identifier)->load();
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IntravenousAudioProcessor();
}
