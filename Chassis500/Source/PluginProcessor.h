#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <sdk500/ModuleRegistry.h>
#include "DSP/ChassisBackplaneDSP.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

class LBX500ChassisAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kNumSlots = lbx500::dsp::ChassisBackplaneDSP::kNumSlots;

    LBX500ChassisAudioProcessor();
    ~LBX500ChassisAudioProcessor() override = default;

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

    // --- 500-Series Rack Slot Management (Message Thread API, Real-Time Safe) ---
    void setSlotModule (int slotIndex, const juce::String& moduleId);
    void swapSlots (int slotA, int slotB);
    void setSlotBypassed (int slotIndex, bool bypassed);
    void setSlotSoloed (int slotIndex, bool soloed);

    [[nodiscard]] std::shared_ptr<sdk500::Module500> getSlotModule (int slotIndex) const;
    [[nodiscard]] bool isSlotBypassed (int slotIndex) const;
    [[nodiscard]] bool isSlotSoloed (int slotIndex) const;

    /** Called periodically by Editor timer to free retired modules off the real-time audio thread. */
    void collectRetiredModules();

    [[nodiscard]] std::uint64_t getRackRevision() const noexcept
    {
        return rackRevision_.load (std::memory_order_acquire);
    }

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    const lbx500::dsp::ChassisBackplaneDSP& getBackplaneDSP() const noexcept { return backplane_; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    lbx500::dsp::ChassisMasterParams readMasterParams() const noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    lbx500::dsp::ChassisBackplaneDSP   backplane_;

    // SpinLock guards <30ns pointer-copy snapshot of the 6 slots; modules are never deleted on audio thread!
    mutable juce::SpinLock slotLock_;
    std::array<lbx500::dsp::SlotRuntimeEntry, kNumSlots> slots_ {};
    std::vector<std::shared_ptr<sdk500::Module500>> retiredPool_;
    std::atomic<std::uint64_t> rackRevision_ { 1 };

    double currentSampleRate_ { 48000.0 };
    int    currentBlockSize_  { 512 };

    // Cached Master PSU Bay parameter pointers
    std::atomic<float>* inTrimParam_     { nullptr };
    std::atomic<float>* outTrimParam_    { nullptr };
    std::atomic<float>* xtalkParam_      { nullptr };
    std::atomic<float>* psuModeParam_    { nullptr };
    std::atomic<float>* term600Param_    { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LBX500ChassisAudioProcessor)
};
