/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
*/
class IntravenousAudioProcessor  : public juce::AudioProcessor
{
    std::vector<float> _output;
    juce::AudioProcessorValueTreeState _value_tree_state;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntravenousAudioProcessor)

public:
    static juce::String const INPUT_GAIN_IDENTIFIER;
    static juce::String const OUTPUT_GAIN_IDENTIFIER;
    static juce::String const WARP_THRESHOLD_IDENTIFIER;
    static juce::String const WARP_SCALE_IDENTIFIER;
    static juce::String const WARP_OFFSET_IDENTIFIER;

    //==============================================================================
    IntravenousAudioProcessor();
    ~IntravenousAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
   #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void recenter_waveform(float& output_sample, float const input_sample) const;
    void warp_waveform(float& output_sample) const;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState();
    float get_parameter(juce::StringRef const parameter_identifier) const;
};
