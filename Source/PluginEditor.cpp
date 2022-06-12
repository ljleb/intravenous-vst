/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
IntravenousAudioProcessorEditor::IntravenousAudioProcessorEditor(IntravenousAudioProcessor& p):
    AudioProcessorEditor(&p),
    _audio_processor(p),
    _input_gain_attachement(p.getValueTreeState(), "input_gain", _input_gain_slider),
    _output_gain_attachement(p.getValueTreeState(), "output_gain", _output_gain_slider)
{
    _input_gain_slider.setRange(0., 10.);
    _input_gain_slider.setSliderStyle(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag);
    _input_gain_slider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxBelow, false, 100, 25);
    _input_gain_slider.setBounds(5, 5, 105, 105);
    addAndMakeVisible(_input_gain_slider);

    _output_gain_slider.setRange(0., 1.);
    _output_gain_slider.setSliderStyle(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag);
    _output_gain_slider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxBelow, false, 100, 25);
    _output_gain_slider.setBounds(110, 5, 105, 105);
    addAndMakeVisible(_output_gain_slider);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (440, 140);
}

IntravenousAudioProcessorEditor::~IntravenousAudioProcessorEditor()
{
}

//==============================================================================
void IntravenousAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void IntravenousAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
}
