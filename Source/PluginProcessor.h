#pragma once
#include <JuceHeader.h>
#include "../Intravenous_NodeGraph/NodeGraph.h"

class IntravenousAudioProcessor final: public juce::AudioProcessor {
    juce::AudioProcessorValueTreeState _value_tree_state;

    std::atomic<float>* _warp_threshold;
    std::atomic<float>* _noise_level;
    std::atomic<float>* _iir_outw0;

    iv::Sample* _channels[2];
    std::vector<std::array<iv::MidiMessage, 128>> _midi_buffers;
    std::vector<uint8_t> _midi_buffer_sizes;
    double _update_frequency;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntravenousAudioProcessor)

public:
    static juce::String const WARP_THRESHOLD_ID;
    static juce::String const NOISE_LEVEL_ID;
    static juce::String const IIR_OUTW0_ID;

    IntravenousAudioProcessor();
    ~IntravenousAudioProcessor() override;

    void prepareToPlay(double, int) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout&) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    iv::GraphNode init_graph();

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
