/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "CallbackParameterListener.h"

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
                "input_gain",
                "Input Gain",
                juce::NormalisableRange<float>(0.f, 10.f),
                1.f),
            std::make_unique<juce::AudioParameterFloat>(
                "output_gain",
                "Output Gain",
                juce::NormalisableRange<float>(0.f, 1.f),
                .5f),
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
    _integrated_samples.resize(getTotalNumInputChannels(), 0.0);
}

void IntravenousAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.

    decltype(_integrated_samples) temporary_samples;
    _integrated_samples.swap(temporary_samples);
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
            auto const sample = buffer.getSample(channel, sample_index);

            if (sample == 0) _integrated_samples[channel] *= 0.999f;

            _integrated_samples[channel] += sample * _value_tree_state.getRawParameterValue("input_gain")->load();

            if (_integrated_samples[channel] > 0)
                _integrated_samples[channel] = std::fmodf(_integrated_samples[channel] + 1.f, 2) - 1.f;
            else
                _integrated_samples[channel] = 1.f - std::fmodf(-_integrated_samples[channel] + 1.f, 2);

            channelData[sample_index] = _integrated_samples[channel] * _value_tree_state.getRawParameterValue("output_gain")->load();
        }
    }
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

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IntravenousAudioProcessor();
}
