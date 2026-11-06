#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "UI/NeveLookAndFeel.h"
#include "UI/VuMeterComponent.h"

class Neve1073PreampAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit Neve1073PreampAudioProcessorEditor (Neve1073PreampAudioProcessor&);
    ~Neve1073PreampAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureKnob (juce::Slider& slider, bool marconiRed);

    Neve1073PreampAudioProcessor& processorRef_;
    nv73::ui::NeveLookAndFeel lookAndFeel_;

    // Controls
    juce::Slider gainStepSlider_;
    juce::Slider faderDriveSlider_;
    juce::Slider biasTrimSlider_;
    juce::Slider outputTrimSlider_;

    juce::ToggleButton loZButton_       { "300\xce\xa9 LO-Z" };
    juce::ToggleButton phaseButton_     { "PHASE \xc3\x98" };
    juce::ToggleButton autoGainButton_  { "AUTO-GAIN" };
    juce::ToggleButton noiseButton_     { "EIN HISS" };
    juce::ComboBox     oversampleBox_;

    nv73::ui::VuMeterComponent meter_;

    // APVTS Attachments
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment>   gainAttachment_;
    std::unique_ptr<SliderAttachment>   faderAttachment_;
    std::unique_ptr<SliderAttachment>   biasAttachment_;
    std::unique_ptr<SliderAttachment>   outputAttachment_;
    std::unique_ptr<ButtonAttachment>   loZAttachment_;
    std::unique_ptr<ButtonAttachment>   phaseAttachment_;
    std::unique_ptr<ButtonAttachment>   autoGainAttachment_;
    std::unique_ptr<ButtonAttachment>   noiseAttachment_;
    std::unique_ptr<ComboBoxAttachment> oversampleAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Neve1073PreampAudioProcessorEditor)
};
