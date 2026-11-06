#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "UI/SlotContainerComponent.h"
#include "UI/MasterPsuBayComponent.h"

#include <array>
#include <memory>

class LBX500ChassisAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                public juce::DragAndDropContainer,
                                                private juce::Timer
{
public:
    explicit LBX500ChassisAudioProcessorEditor (LBX500ChassisAudioProcessor&);
    ~LBX500ChassisAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    LBX500ChassisAudioProcessor& processorRef_;
    std::uint64_t lastSeenRevision_ { 0 };

    std::array<std::unique_ptr<lbx500::ui::SlotContainerComponent>, LBX500ChassisAudioProcessor::kNumSlots> slotUIs_;
    lbx500::ui::MasterPsuBayComponent masterBay_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LBX500ChassisAudioProcessorEditor)
};
