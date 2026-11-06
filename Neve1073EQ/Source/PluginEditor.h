#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <UI/NeveLookAndFeel.h>

class Neve1073EQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit Neve1073EQAudioProcessorEditor (Neve1073EQAudioProcessor&);
    ~Neve1073EQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawResponseDisplay (juce::Graphics& g, juce::Rectangle<float> area) const;
    void drawMeterStrip (juce::Graphics& g, juce::Rectangle<float> area) const;

    Neve1073EQAudioProcessor& processorRef_;
    nv73::ui::NeveLookAndFeel lnf_;

    juce::Slider   hfKnob_, midKnob_, lowKnob_, trimKnob_;
    juce::ComboBox midFreqBox_, lowFreqBox_, hpfBox_, oversampleBox_;
    juce::ToggleButton eqInBtn_  { "EQL (EQ IN)" };
    juce::ToggleButton phaseBtn_ { "\xc3\x98 PHASE" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> hfAttach_, midGainAttach_, lowGainAttach_, trimAttach_;
    std::unique_ptr<ComboAttachment>  midFreqAttach_, lowFreqAttach_, hpfAttach_, osAttach_;
    std::unique_ptr<ButtonAttachment> eqInAttach_, phaseAttach_;

    float inLinear_  { 0.0f };
    float outLinear_ { 0.0f };
    float glowNorm_  { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Neve1073EQAudioProcessorEditor)
};
