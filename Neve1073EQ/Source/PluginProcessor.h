#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Neve1073EQDSP.h"

class Neve1073EQAudioProcessor final : public juce::AudioProcessor
{
public:
    Neve1073EQAudioProcessor();
    ~Neve1073EQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    const nv73eq::dsp::Neve1073EQDSP& getDSP() const noexcept { return dsp_; }
    nv73eq::dsp::EQParams readCurrentParams() const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    juce::AudioProcessorValueTreeState apvts_;
    nv73eq::dsp::Neve1073EQDSP dsp_;

    std::atomic<float>* hfGainParam_      { nullptr };
    std::atomic<float>* midFreqParam_     { nullptr };
    std::atomic<float>* midGainParam_     { nullptr };
    std::atomic<float>* lowFreqParam_     { nullptr };
    std::atomic<float>* lowGainParam_     { nullptr };
    std::atomic<float>* hpfFreqParam_     { nullptr };
    std::atomic<float>* outputTrimParam_  { nullptr };
    std::atomic<float>* eqInParam_        { nullptr };
    std::atomic<float>* phaseInvertParam_ { nullptr };
    std::atomic<float>* oversampleParam_  { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Neve1073EQAudioProcessor)
};
