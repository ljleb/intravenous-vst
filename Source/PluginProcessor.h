#pragma once
#include <JuceHeader.h>
#include "../Intravenous_NodeGraph/public.h"


class IntravenousAudioProcessor final: public juce::AudioProcessor {
    juce::AudioProcessorValueTreeState _value_tree_state;

    std::atomic<float>* _uniform_noise_level;
    std::atomic<float>* _time_offset;
    std::atomic<float>* _time_window;
    std::atomic<float>* _gaussian_noise_ratio;
    std::atomic<float>* _harmonics_noise_ratio;
    std::atomic<float>* _noise_low_pass;
    std::atomic<float>* _noise_high_pass;

    iv::Sample* _channels[2];
    std::vector<std::array<iv::MidiMessage, 128>> _midi_buffers;
    std::vector<uint8_t> _midi_buffer_sizes;
    double _update_frequency;
    size_t _song_index;
    iv::NodeProcessor* _node_processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntravenousAudioProcessor)

public:
    static juce::String const UNIFORM_NOISE_LEVEL_ID;
    static juce::String const TIME_WINDOW_ID;
    static juce::String const TIME_OFFSET_ID;
    static juce::String const GAUSSIAN_NOISE_RATIO_ID;
    static juce::String const HARMONICS_NOISE_RATIO_ID;
    static juce::String const NOISE_LOW_PASS_ID;
    static juce::String const NOISE_HIGH_PASS_ID;

    IntravenousAudioProcessor();
    ~IntravenousAudioProcessor() override;

    void prepareToPlay(double, int) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout&) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

public:
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
};
