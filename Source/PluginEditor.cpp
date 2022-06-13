/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

IntravenousAudioProcessorEditor::IntravenousAudioProcessorEditor(IntravenousAudioProcessor& p):
    AudioProcessorEditor(&p),
    _audio_processor(p)
{
    for (auto const& parameter_identifier : {
        IntravenousAudioProcessor::INTEGRAL_IDENTIFIER,
        IntravenousAudioProcessor::INPUT_GAIN_IDENTIFIER,
        IntravenousAudioProcessor::WARP_SCALE_IDENTIFIER,
        IntravenousAudioProcessor::WARP_OFFSET_IDENTIFIER,
        IntravenousAudioProcessor::WARP_THRESHOLD_IDENTIFIER,
        IntravenousAudioProcessor::OUTPUT_GAIN_IDENTIFIER,
    }) {
        _slider_packs.emplace_back(
            std::make_unique<SliderPack>(
                *this,
                p.getValueTreeState(),
                parameter_identifier,
                static_cast<unsigned int>(_slider_packs.size())));
    }

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setResizable(false, false);
    setSize(static_cast<unsigned int>(_slider_packs.size() * 100), 140);
}

IntravenousAudioProcessorEditor::~IntravenousAudioProcessorEditor()
{
}

IntravenousAudioProcessorEditor::SliderPack::SliderPack(
    IntravenousAudioProcessorEditor& editor,
    juce::AudioProcessorValueTreeState& value_tree_state,
    juce::StringRef const parameter_identifier,
    unsigned int slider_position
):
    _slider(parameter_identifier),
    _attachement(value_tree_state, parameter_identifier, _slider)
{
    _slider.setBounds(100 * slider_position, 25, 105, 105);
    _slider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxBelow, false, 64, 20);
    _slider.setSliderStyle(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag);

    auto const& parameter = value_tree_state.getParameter(_slider.getName());
    auto const& range = parameter->getNormalisableRange().getRange();
    _slider.setRange(range.getStart(), range.getEnd());
    editor.addAndMakeVisible(_slider);

    _label.attachToComponent(&_slider, false);
    _label.setJustificationType(juce::Justification::centred);
    _label.setFont(juce::Font(16.f));
    _label.setText(parameter->getName(20), juce::NotificationType::dontSendNotification);
    editor.addAndMakeVisible(_label);
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
