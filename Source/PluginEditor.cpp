/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

IntravenousAudioProcessorEditor::IntravenousAudioProcessorEditor(IntravenousAudioProcessor& p):
    AudioProcessorEditor(&p),
    _audio_processor(p),
    _input_gain_slider(IntravenousAudioProcessor::INPUT_GAIN_IDENTIFIER),
    _input_gain_attachement(p.getValueTreeState(), IntravenousAudioProcessor::INPUT_GAIN_IDENTIFIER, _input_gain_slider),
    _output_gain_slider(IntravenousAudioProcessor::OUTPUT_GAIN_IDENTIFIER),
    _output_gain_attachement(p.getValueTreeState(), IntravenousAudioProcessor::OUTPUT_GAIN_IDENTIFIER, _output_gain_slider),
    _warp_scale_slider(IntravenousAudioProcessor::WARP_SCALE_IDENTIFIER),
    _warp_scale_attachement(p.getValueTreeState(), IntravenousAudioProcessor::WARP_SCALE_IDENTIFIER, _warp_scale_slider),
    _warp_offset_slider(IntravenousAudioProcessor::WARP_OFFSET_IDENTIFIER),
    _warp_offset_attachement(p.getValueTreeState(), IntravenousAudioProcessor::WARP_OFFSET_IDENTIFIER, _warp_offset_slider)
{
    initialize_slider(_input_gain_slider, _input_gain_label, 0, p.getValueTreeState());
    initialize_slider(_output_gain_slider, _ouput_gain_label, 1, p.getValueTreeState());
    initialize_slider(_warp_scale_slider, _warp_scale_label, 2, p.getValueTreeState());
    initialize_slider(_warp_offset_slider, _warp_offset_label, 3, p.getValueTreeState());

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setResizable(false, false);
    setSize(440, 140);
}

IntravenousAudioProcessorEditor::~IntravenousAudioProcessorEditor()
{
}

void IntravenousAudioProcessorEditor::initialize_slider(juce::Slider& slider, juce::Label& label, uint32_t slider_position, juce::AudioProcessorValueTreeState& value_tree_state)
{
    slider.setBounds(100 * slider_position, 25, 105, 105);
    slider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxBelow, false, 64, 20);
    slider.setSliderStyle(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag);

    auto const& parameter = value_tree_state.getParameter(slider.getName());
    auto const& range = parameter->getNormalisableRange().getRange();
    slider.setRange(range.getStart(), range.getEnd());
    addAndMakeVisible(slider);

    addAndMakeVisible(label);
    label.setText(parameter->getName(20), juce::NotificationType::dontSendNotification);
    label.attachToComponent(&slider, false);
    label.setFont(juce::Font(16.f));
    label.setJustificationType(juce::Justification::centred);
}

void IntravenousAudioProcessorEditor::paint(juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void IntravenousAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
}
