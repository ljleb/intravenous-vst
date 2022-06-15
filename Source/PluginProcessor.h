#pragma once
#include <JuceHeader.h>

class IntravenousAudioProcessor final: public juce::AudioProcessor {
    std::vector<float> _last_output;
    std::vector<float> _low_passed_last_output;

    std::vector<float> _input_loudness;

    juce::AudioProcessorValueTreeState _value_tree_state;

    std::atomic<float>* _integral_gain;
    std::atomic<float>* _dc_offset_gain;
    std::atomic<float>* _invert_warp_gain;
    std::atomic<float>* _dry_gain;
    std::atomic<float>* _wet_gain;
    std::atomic<float>* _input_gain;
    std::atomic<float>* _input_offset;
    std::atomic<float>* _input_offset_decay;
    std::atomic<float>* _warp_threshold;
    std::atomic<float>* _warp_pre_gain;
    std::atomic<float>* _warp_destination;
    std::atomic<float>* _warp_scale;

    std::chrono::high_resolution_clock::time_point _begin;
    std::chrono::high_resolution_clock::time_point _end;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntravenousAudioProcessor)

public:
    static juce::String const INTEGRATE_IDENTIFIER;
    static juce::String const REMOVE_DC_OFFSET_IDENTIFIER;
    static juce::String const INVERT_WARP_IDENTIFIER;
    static juce::String const DRY_GAIN_IDENTIFIER;
    static juce::String const WET_GAIN_IDENTIFIER;
    static juce::String const INPUT_GAIN_IDENTIFIER;
    static juce::String const INPUT_OFFSET_IDENTIFIER;
    static juce::String const INPUT_OFFSET_DECAY_IDENTIFIER;
    static juce::String const WARP_THRESHOLD_IDENTIFIER;
    static juce::String const WARP_PRE_GAIN_IDENTIFIER;
    static juce::String const WARP_DESTINATION_IDENTIFIER;
    static juce::String const WARP_SCALE_IDENTIFIER;

    IntravenousAudioProcessor();
    ~IntravenousAudioProcessor() override;

    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void clearSideEffects();

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout&) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    void update_input_loudness(float const&, float const&, float const&, float&);
    float remove_dc_offset(float const&, float const&, float&) const;
    float warp_sample(float const&, float const&, float const&, float const&, float const&, float const&) const;
    float warp_positive_sample(float const&, float const&, float const&, float const&, float const&, float const&) const;

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
