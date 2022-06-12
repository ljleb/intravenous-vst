/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class IntravenousAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    IntravenousAudioProcessorEditor (IntravenousAudioProcessor&);
    ~IntravenousAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    IntravenousAudioProcessor& _audio_processor;

    juce::Slider _input_gain_slider;
    juce::AudioProcessorValueTreeState::SliderAttachment _input_gain_attachement;
    juce::Slider _output_gain_slider;
    juce::AudioProcessorValueTreeState::SliderAttachment _output_gain_attachement;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntravenousAudioProcessorEditor)
};
