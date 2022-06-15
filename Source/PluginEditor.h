#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class IntravenousAudioProcessorEditor final: public juce::AudioProcessorEditor {
public:
    IntravenousAudioProcessorEditor(IntravenousAudioProcessor&, juce::AudioProcessorValueTreeState&);
    ~IntravenousAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class SliderPack {
        juce::Slider _slider;
        juce::Label _label;
        juce::AudioProcessorValueTreeState::SliderAttachment _attachment;

    public:
        SliderPack(
            IntravenousAudioProcessorEditor& editor,
            juce::AudioProcessorValueTreeState& value_tree_state,
            juce::StringRef const parameter_identifier,
            unsigned int slider_position);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliderPack)
    };

    class ButtonPack {
        juce::ToggleButton _button;
        juce::AudioProcessorValueTreeState::ButtonAttachment _attachment;

    public:
        ButtonPack(
            IntravenousAudioProcessorEditor& editor,
            juce::AudioProcessorValueTreeState& value_tree_state,
            juce::StringRef const parameter_identifier,
            unsigned int slider_position);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ButtonPack)
    };

    IntravenousAudioProcessor& _audio_processor;
    std::vector<std::unique_ptr<SliderPack>> _slider_packs;
    std::vector<std::unique_ptr<ButtonPack>> _button_packs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntravenousAudioProcessorEditor)
};
