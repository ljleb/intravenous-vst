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
    void initialize_slider(juce::Slider& slider, juce::Label& label, uint32_t slider_position, juce::AudioProcessorValueTreeState& value_tree_state);

    IntravenousAudioProcessor& _audio_processor;

    juce::Slider _input_gain_slider;
    juce::Label _input_gain_label;
    juce::AudioProcessorValueTreeState::SliderAttachment _input_gain_attachement;

    juce::Slider _output_gain_slider;
    juce::Label _ouput_gain_label;
    juce::AudioProcessorValueTreeState::SliderAttachment _output_gain_attachement;

    juce::Slider _warp_scale_slider;
    juce::Label _warp_scale_label;
    juce::AudioProcessorValueTreeState::SliderAttachment _warp_scale_attachement;

    juce::Slider _warp_offset_slider;
    juce::Label _warp_offset_label;
    juce::AudioProcessorValueTreeState::SliderAttachment _warp_offset_attachement;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntravenousAudioProcessorEditor)
};
