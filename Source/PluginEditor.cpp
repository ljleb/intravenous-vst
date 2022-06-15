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
        IntravenousAudioProcessor::WARP_PRE_GAIN_IDENTIFIER,
        IntravenousAudioProcessor::WARP_DESTINATION_IDENTIFIER,
        IntravenousAudioProcessor::WARP_SCALE_IDENTIFIER,
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

    for (auto const& parameter_identifier: {
        IntravenousAudioProcessor::INTEGRATE_IDENTIFIER,
        IntravenousAudioProcessor::REMOVE_DC_OFFSET_IDENTIFIER,
        IntravenousAudioProcessor::INVERT_WARP_IDENTIFIER,
    }) {
        _button_packs.emplace_back(
            std::make_unique<ButtonPack>(
                *this,
                value_tree_state,
                parameter_identifier,
                static_cast<unsigned int>(_button_packs.size())));
    }

    setResizable(false, false);
    setSize(static_cast<unsigned int>((_slider_packs.size() + 1) * 100), 140);
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
    _attachment(value_tree_state, parameter_identifier, _slider)
{
    _slider.setBounds(100 * (slider_position + 1), 25, 105, 105);
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

IntravenousAudioProcessorEditor::ButtonPack::ButtonPack(
    IntravenousAudioProcessorEditor& editor,
    juce::AudioProcessorValueTreeState& value_tree_state,
    juce::StringRef const parameter_identifier,
    unsigned int button_position
):
    _attachment(value_tree_state, parameter_identifier, _button)
{
    auto const& parameter = value_tree_state.getParameter(parameter_identifier);
    _button.setBounds(0, 25 * button_position, 105, 25);
    _button.setButtonText(parameter->getName(20));
    editor.addAndMakeVisible(_button);
}

void IntravenousAudioProcessorEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void IntravenousAudioProcessorEditor::resized() {
}
