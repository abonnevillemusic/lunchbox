#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Neve1073PreampDSP.h"

class Neve1073PreampAudioProcessor final : public juce::AudioProcessor
{
public:
    Neve1073PreampAudioProcessor();
    ~Neve1073PreampAudioProcessor() override = default;

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
    const nv73::dsp::Neve1073PreampDSP& getDSP() const noexcept { return dsp_; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    nv73::dsp::PreampParams readCurrentParams() const noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    nv73::dsp::Neve1073PreampDSP dsp_;

    // Cached lock-free raw parameter pointers
    std::atomic<float>* gainStepParam_    { nullptr };
    std::atomic<float>* faderDriveParam_  { nullptr };
    std::atomic<float>* outputTrimParam_  { nullptr };
    std::atomic<float>* biasTrimParam_    { nullptr };
    std::atomic<float>* loZParam_         { nullptr };
    std::atomic<float>* phaseParam_       { nullptr };
    std::atomic<float>* autoGainParam_    { nullptr };
    std::atomic<float>* analogNoiseParam_ { nullptr };
    std::atomic<float>* oversampleParam_  { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Neve1073PreampAudioProcessor)
};
