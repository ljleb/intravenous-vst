#pragma once
#include <JuceHeader.h>

struct tuple_hash {
    template <typename T1, typename T2>
    size_t operator()(const std::tuple<T1, T2>& x) const
    {
        return std::get<0>(x) ^ std::get<1>(x);
    }
};

class IntravenousAudioProcessor final: public juce::AudioProcessor {
    // [audio channel][note number, midi channel] = sample
    std::vector<std::unordered_map<std::tuple<int, int>, float, tuple_hash>> _voices;
    std::vector<std::queue<float>> _latency_buffers;
    std::unordered_map<size_t, std::vector<juce::MidiMessage>> _unordered_midi;

    juce::AudioProcessorValueTreeState _value_tree_state;

    std::atomic<float>* _warp_threshold;
    std::atomic<float>* _noise_level;

    // [note number, midi channel] = [superposed, velocity]
    std::unordered_map<std::tuple<int, int>, std::vector<unsigned long long>, tuple_hash> _note_velocities;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntravenousAudioProcessor)

public:
    static juce::String const WARP_THRESHOLD_ID;
    static juce::String const NOISE_LEVEL_ID;

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
    float accumulate_step(float const&, float const&, float const&, float const&) const;
    std::tuple<bool, float> warp_sample(float, float const&) const;
    float warp_positive_sample(float const&, float const&) const;

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
