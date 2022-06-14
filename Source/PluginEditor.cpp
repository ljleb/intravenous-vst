#include "PluginProcessor.h"
#include "PluginEditor.h"

IntravenousAudioProcessorEditor::IntravenousAudioProcessorEditor(IntravenousAudioProcessor& audio_processor, juce::AudioProcessorValueTreeState& value_tree_state):
    AudioProcessorEditor(&audio_processor),
    _audio_processor(audio_processor)
{
    for (auto const& parameter_identifier: {
        IntravenousAudioProcessor::INPUT_GAIN_IDENTIFIER,
        IntravenousAudioProcessor::INPUT_OFFSET_IDENTIFIER,
        IntravenousAudioProcessor::INPUT_OFFSET_DECAY_IDENTIFIER,
        IntravenousAudioProcessor::WARP_THRESHOLD_IDENTIFIER,
        IntravenousAudioProcessor::WARP_SCALE_IDENTIFIER,
        IntravenousAudioProcessor::WARP_DESTINATION_IDENTIFIER,
        IntravenousAudioProcessor::DRY_GAIN_IDENTIFIER,
        IntravenousAudioProcessor::WET_GAIN_IDENTIFIER,
    }) {
        _slider_packs.emplace_back(
            std::make_unique<SliderPack>(
                *this,
                value_tree_state,
                parameter_identifier,
                static_cast<unsigned int>(_slider_packs.size())));
    }

    setResizable(false, false);
    setSize(static_cast<unsigned int>(_slider_packs.size() * 100), 140);
}

IntravenousAudioProcessorEditor::~IntravenousAudioProcessorEditor() {
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
    editor.addAndMakeVisible(_slider);

    auto const& slider_parameter = value_tree_state.getParameter(_slider.getName());
    _label.attachToComponent(&_slider, false);
    _label.setJustificationType(juce::Justification::centred);
    _label.setFont(juce::Font(16.f));
    _label.setText(slider_parameter->getName(20), juce::NotificationType::dontSendNotification);
    editor.addAndMakeVisible(_label);
}

void IntravenousAudioProcessorEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void IntravenousAudioProcessorEditor::resized() {
}
