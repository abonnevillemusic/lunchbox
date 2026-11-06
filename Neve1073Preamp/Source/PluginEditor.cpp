#include "PluginEditor.h"

Neve1073PreampAudioProcessorEditor::Neve1073PreampAudioProcessorEditor (Neve1073PreampAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef_ (p)
{
    setLookAndFeel (&lookAndFeel_);

    configureKnob (gainStepSlider_,   true);
    configureKnob (faderDriveSlider_, false);
    configureKnob (biasTrimSlider_,   false);
    configureKnob (outputTrimSlider_, false);

    addAndMakeVisible (gainStepSlider_);
    addAndMakeVisible (faderDriveSlider_);
    addAndMakeVisible (biasTrimSlider_);
    addAndMakeVisible (outputTrimSlider_);

    for (auto* btn : { &loZButton_, &phaseButton_, &autoGainButton_, &noiseButton_ })
        addAndMakeVisible (*btn);

    oversampleBox_.addItemList ({ "OS: 1x (Off)", "OS: 2x", "OS: 4x (HQ)" }, 1);
    addAndMakeVisible (oversampleBox_);
    addAndMakeVisible (meter_);

    auto& apvts = processorRef_.getAPVTS();
    gainAttachment_       = std::make_unique<SliderAttachment>   (apvts, "gainStep",     gainStepSlider_);
    faderAttachment_      = std::make_unique<SliderAttachment>   (apvts, "faderDrive",   faderDriveSlider_);
    biasAttachment_       = std::make_unique<SliderAttachment>   (apvts, "biasTrim",     biasTrimSlider_);
    outputAttachment_     = std::make_unique<SliderAttachment>   (apvts, "outputTrim",   outputTrimSlider_);
    loZAttachment_        = std::make_unique<ButtonAttachment>   (apvts, "loZ",          loZButton_);
    phaseAttachment_      = std::make_unique<ButtonAttachment>   (apvts, "phaseInvert",  phaseButton_);
    autoGainAttachment_   = std::make_unique<ButtonAttachment>   (apvts, "autoGain",     autoGainButton_);
    noiseAttachment_      = std::make_unique<ButtonAttachment>   (apvts, "analogNoise",  noiseButton_);
    oversampleAttachment_ = std::make_unique<ComboBoxAttachment> (apvts, "oversampling", oversampleBox_);

    setSize (720, 430);
    startTimerHz (30);
}

Neve1073PreampAudioProcessorEditor::~Neve1073PreampAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Neve1073PreampAudioProcessorEditor::configureKnob (juce::Slider& slider, bool marconiRed)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, marconiRed ? 110 : 86, 22);
    slider.getProperties().set ("marconiRed", marconiRed);
}

void Neve1073PreampAudioProcessorEditor::timerCallback()
{
    const auto& dsp = processorRef_.getDSP();
    meter_.setLevels (dsp.getInputMeterLinear(),
                      dsp.getOutputMeterLinear(),
                      dsp.getSaturationMeter(),
                      dsp.isThreeStageActive());
}

void Neve1073PreampAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace nv73::ui;

    const auto bounds = getLocalBounds().toFloat();

    // 1. Brushed aluminum top/bottom rack rails
    g.setColour (NeveLookAndFeel::kFaceplateDark);
    g.fillAll();

    juce::ColourGradient panelGrad (NeveLookAndFeel::kFaceplateBase.brighter (0.06f), 0.0f, 12.0f,
                                    NeveLookAndFeel::kFaceplateBase.darker (0.16f),   0.0f, bounds.getHeight() - 12.0f, false);
    g.setGradientFill (panelGrad);
    g.fillRoundedRectangle (bounds.reduced (8.0f), 6.0f);

    g.setColour (NeveLookAndFeel::kRailSilver.withAlpha (0.45f));
    g.drawRoundedRectangle (bounds.reduced (8.0f), 6.0f, 1.5f);

    // Corner rack mounting screws
    for (auto pt : { juce::Point<float> (22.0f, 22.0f),
                     juce::Point<float> (bounds.getWidth() - 22.0f, 22.0f),
                     juce::Point<float> (22.0f, bounds.getHeight() - 22.0f),
                     juce::Point<float> (bounds.getWidth() - 22.0f, bounds.getHeight() - 22.0f) })
    {
        g.setColour (juce::Colour (0xff1c2228));
        g.fillEllipse (pt.x - 5.5f, pt.y - 5.5f, 11.0f, 11.0f);
        g.setColour (NeveLookAndFeel::kRailSilver.withAlpha (0.7f));
        g.fillEllipse (pt.x - 4.0f, pt.y - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colour (0xff2c343c));
        g.drawLine (pt.x - 2.8f, pt.y - 2.8f, pt.x + 2.8f, pt.y + 2.8f, 1.3f);
    }

    // 2. Header nameplate
    g.setColour (NeveLookAndFeel::kCreamLegend);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("NV-73  CLASS-A MICROPHONE PREAMPLIFIER", 38, 16, 480, 26, juce::Justification::centredLeft);

    g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.68f));
    g.setFont (juce::FontOptions (11.5f, juce::Font::plain));
    g.drawText ("T10468 PERMALLOY INPUT  \xe2\x80\xa2  BA283AV / BA284 DISCRETE BJT  \xe2\x80\xa2  2N3055 + LO1166 OUTPUT",
                38, 40, 520, 18, juce::Justification::centredLeft);

    // Subtle vertical divider between Input Sensitivity section and Output Stage section
    g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.18f));
    g.drawVerticalLine (268, 74.0f, 312.0f);

    // 3. Section & Knob Legends
    g.setColour (NeveLookAndFeel::kCreamLegend);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText ("SENSITIVITY (PL20033)", 34, 72, 215, 20, juce::Justification::centred);
    g.drawText ("FADER DRIVE",           290, 86, 130, 20, juce::Justification::centred);
    g.drawText ("RV1 CLASS-A BIAS",      430, 86, 130, 20, juce::Justification::centred);
    g.drawText ("OUTPUT PAD",            570, 86, 120, 20, juce::Justification::centred);

    g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.62f));
    g.setFont (juce::FontOptions (10.5f, juce::Font::plain));
    g.drawText ("50\xe2\x86\x92" "55 dB ENGAGES 3-STAGE", 34, 282, 215, 16, juce::Justification::centred);
    g.drawText ("INTO BA283AM 2N3055",  290, 256, 130, 16, juce::Justification::centred);
    g.drawText ("22.6Vpp SYMMETRY",     430, 256, 130, 16, juce::Justification::centred);
    g.drawText ("POST-LO1166 TRIM",     570, 256, 120, 16, juce::Justification::centred);
}

void Neve1073PreampAudioProcessorEditor::resized()
{
    // Left: Iconic large Red Marconi Gain Switch
    gainStepSlider_.setBounds (52, 94, 180, 184);

    // Right: Three Grey Marconi Console Knobs
    faderDriveSlider_.setBounds (295, 108, 120, 144);
    biasTrimSlider_.setBounds   (435, 108, 120, 144);
    outputTrimSlider_.setBounds (570, 108, 120, 144);

    // Top-right Oversampling selector
    oversampleBox_.setBounds (566, 20, 124, 26);

    // Bottom Console Illuminated Pushbuttons + Metering Panel
    loZButton_.setBounds      (36,  324, 106, 34);
    phaseButton_.setBounds    (150, 324, 102, 34);
    autoGainButton_.setBounds (36,  366, 106, 34);
    noiseButton_.setBounds    (150, 366, 102, 34);

    meter_.setBounds (284, 318, 404, 84);
}
