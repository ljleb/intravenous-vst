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
    _input_gain_slider(IntravenousAudioProcessor::INPUT_GAIN_IDENTIFIER),
    _input_gain_attachement(p.getValueTreeState(), IntravenousAudioProcessor::INPUT_GAIN_IDENTIFIER, _input_gain_slider),
    _output_gain_slider(IntravenousAudioProcessor::OUTPUT_GAIN_IDENTIFIER),
    _output_gain_attachement(p.getValueTreeState(), IntravenousAudioProcessor::OUTPUT_GAIN_IDENTIFIER, _output_gain_slider)
{
    initialize_slider(_input_gain_slider, 0, p.getValueTreeState());
    initialize_slider(_output_gain_slider, 1, p.getValueTreeState());

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(440, 140);
}

IntravenousAudioProcessorEditor::~IntravenousAudioProcessorEditor()
{
}

void IntravenousAudioProcessorEditor::initialize_slider(juce::Slider& slider, uint32_t slider_position, juce::AudioProcessorValueTreeState& value_tree_state)
{
    auto const& parameter = value_tree_state.getParameter(slider.getName());
    auto const& range = parameter->getNormalisableRange().getRange();
    slider.setRange(range.getStart(), range.getEnd());
    slider.setSliderStyle(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxBelow, false, 100, 25);
    slider.setBounds(5 + 105 * slider_position, 5, 105, 105);
    addAndMakeVisible(slider);
}

//==============================================================================
void IntravenousAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void IntravenousAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
}
