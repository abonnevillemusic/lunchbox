#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include <NV73Module500.h>

#include <algorithm>
#include <cmath>

namespace lbx500::ui
{

/**
 * Right-hand 7th bay of the LBX-500 Lunchbox:
 *   - Live +/-16.0V DC Rail Voltage & Total mA Load (0 .. 780+ mA) PSU Telemetry Gauges
 *   - PSU Topology Selector (Vintage Shared Linear vs. Isolated OST Regulated)
 *   - 600-Ohm Master Output Transformer Load Switch
 *   - Backplane Crosstalk (X-TALK), Chassis Input Trim, & Master Output Knobs
 *   - Master IN / OUT Bar Meters
 */
class MasterPsuBayComponent final : public juce::Component
{
public:
    explicit MasterPsuBayComponent (LBX500ChassisAudioProcessor& proc)
        : proc_ (proc)
    {
        setLookAndFeel (&neveLnf_);

        psuModeBox_.addItemList ({ "PSU: Vintage Linear", "PSU: Isolated OST" }, 1);
        addAndMakeVisible (psuModeBox_);

        addAndMakeVisible (term600Btn_);

        setupKnob (xtalkKnob_);
        setupKnob (inTrimKnob_);
        setupKnob (outTrimKnob_);

        auto& apvts = proc_.getAPVTS();
        psuAttach_     = std::make_unique<ComboAttach>  (apvts, "psuMode",      psuModeBox_);
        termAttach_    = std::make_unique<ButtonAttach> (apvts, "terminate600", term600Btn_);
        xtalkAttach_   = std::make_unique<SliderAttach> (apvts, "crosstalk",    xtalkKnob_);
        inTrimAttach_  = std::make_unique<SliderAttach> (apvts, "inputTrim",    inTrimKnob_);
        outTrimAttach_ = std::make_unique<SliderAttach> (apvts, "outputTrim",   outTrimKnob_);
    }

    ~MasterPsuBayComponent() override
    {
        setLookAndFeel (nullptr);
    }

    void updateTelemetry (float currentMa, float railVolts, float inLin, float outLin) noexcept
    {
        currentMa_ = currentMa;
        railVolts_ = railVolts;
        inLin_     = inLin;
        outLin_    = outLin;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        // Dark gunmetal PSU chassis panel
        juce::ColourGradient grad (juce::Colour (0xff232a31), 0.0f, 0.0f,
                                   juce::Colour (0xff151a1f), b.getWidth(), b.getHeight(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, 5.0f);

        g.setColour (juce::Colour (0xff566370));
        g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.2f);

        const auto cream = nv73::ui::NeveLookAndFeel::kCreamLegend;
        g.setColour (cream);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("LBX-500  PSU & MASTER", 8, 8, getWidth() - 16, 18, juce::Justification::centred);

        // 1. +/-16V Rail & Current Load Gauge Box
        const juce::Rectangle<float> psuBox (10.0f, 32.0f, b.getWidth() - 20.0f, 92.0f);
        g.setColour (juce::Colour (0xff101418));
        g.fillRoundedRectangle (psuBox, 4.0f);
        g.setColour (juce::Colour (0xff39434d));
        g.drawRoundedRectangle (psuBox, 4.0f, 1.0f);

        const bool overloaded = currentMa_ > 780.0f;
        // Green +/-16V healthy LED vs Amber/Red sag warning
        g.setColour (overloaded ? juce::Colour (0xffef4444) : juce::Colour (0xff2ecc71));
        g.fillEllipse (psuBox.getRight() - 18.0f, psuBox.getY() + 8.0f, 9.0f, 9.0f);

        g.setColour (cream);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("RAIL: +/-" + juce::String (railVolts_, 2) + " V DC",
                    static_cast<int> (psuBox.getX() + 8), static_cast<int> (psuBox.getY() + 5),
                    155, 16, juce::Justification::centredLeft);

        g.setColour (cream.withAlpha (0.82f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::plain));
        g.drawText ("LOAD: " + juce::String (static_cast<int> (std::round (currentMa_))) + " / 780 mA",
                    static_cast<int> (psuBox.getX() + 8), static_cast<int> (psuBox.getY() + 24),
                    175, 15, juce::Justification::centredLeft);

        // Current mA horizontal bar
        const juce::Rectangle<float> maTrack (psuBox.getX() + 8.0f, psuBox.getY() + 44.0f, psuBox.getWidth() - 16.0f, 10.0f);
        g.setColour (juce::Colour (0xff1d242b));
        g.fillRoundedRectangle (maTrack, 3.0f);
        const float maNorm = std::clamp (currentMa_ / 920.0f, 0.0f, 1.0f);
        if (maNorm > 0.005f)
        {
            juce::Colour barCol = (currentMa_ > 780.0f) ? juce::Colour (0xffef4444)
                                : (currentMa_ > 600.0f) ? juce::Colour (0xfff59e0b)
                                                        : juce::Colour (0xff38bdf8);
            g.setColour (barCol);
            g.fillRoundedRectangle (maTrack.withWidth (maTrack.getWidth() * maNorm), 3.0f);
        }
        // 780 mA VPR spec marker
        const float specX = maTrack.getX() + maTrack.getWidth() * (780.0f / 920.0f);
        g.setColour (cream.withAlpha (0.7f));
        g.drawVerticalLine (static_cast<int> (specX), maTrack.getY() - 2.0f, maTrack.getBottom() + 2.0f);

        // Rail Sag dynamic meter bar (shows voltage droop from 16.0V)
        const float sagVolts = std::max (0.0f, 16.0f - railVolts_);
        g.setColour (cream.withAlpha (0.72f));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText ("PSU SAG: -" + juce::String (sagVolts, 2) + " V",
                    static_cast<int> (psuBox.getX() + 8), static_cast<int> (psuBox.getY() + 62),
                    160, 14, juce::Justification::centredLeft);

        // 2. Knob Labels
        g.setColour (cream);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("X-TALK",      8,   202, getWidth() - 16, 14, juce::Justification::centred);
        g.drawText ("CHASSIS IN",  6,   302, (getWidth() - 12) / 2, 14, juce::Justification::centred);
        g.drawText ("MASTER OUT",  getWidth() / 2, 302, (getWidth() - 12) / 2, 14, juce::Justification::centred);

        // 3. Master IN / OUT Meter Box
        const juce::Rectangle<float> meterBox (10.0f, 418.0f, b.getWidth() - 20.0f, 62.0f);
        g.setColour (juce::Colour (0xff101418));
        g.fillRoundedRectangle (meterBox, 4.0f);
        drawMiniBar (g, juce::Rectangle<float> (meterBox.getX() + 8.0f, meterBox.getY() + 10.0f, meterBox.getWidth() - 16.0f, 16.0f), "IN",  inLin_);
        drawMiniBar (g, juce::Rectangle<float> (meterBox.getX() + 8.0f, meterBox.getY() + 34.0f, meterBox.getWidth() - 16.0f, 16.0f), "OUT", outLin_);
    }

    void resized() override
    {
        const int w = getWidth();
        psuModeBox_.setBounds (10, 132, w - 20, 26);
        term600Btn_.setBounds (10, 164, w - 20, 28);

        xtalkKnob_.setBounds  ((w - 82) / 2, 214, 82, 82);
        const int halfW = (w - 16) / 2;
        inTrimKnob_.setBounds  (8,         316, halfW, 88);
        outTrimKnob_.setBounds (8 + halfW, 316, halfW, 88);
    }

private:
    void setupKnob (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        s.getProperties().set ("marconiRed", false);
        addAndMakeVisible (s);
    }

    static void drawMiniBar (juce::Graphics& g, juce::Rectangle<float> row, const char* lbl, float lin)
    {
        auto lblArea = row.removeFromLeft (28.0f);
        g.setColour (nv73::ui::NeveLookAndFeel::kCreamLegend.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (lbl, lblArea.toNearestInt(), juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xff1d242b));
        g.fillRoundedRectangle (row.reduced (0.0f, 3.0f), 2.5f);

        const float db   = (lin > 1e-5f) ? std::clamp (20.0f * std::log10 (lin), -48.0f, 6.0f) : -48.0f;
        const float norm = (db + 48.0f) / 54.0f;
        if (norm > 0.005f)
        {
            auto fill = row.reduced (0.0f, 3.0f);
            fill.setWidth (fill.getWidth() * norm);
            g.setColour ((db > 0.0f) ? juce::Colour (0xffef4444) : juce::Colour (0xff34d399));
            g.fillRoundedRectangle (fill, 2.5f);
        }
    }

    LBX500ChassisAudioProcessor& proc_;
    nv73::ui::NeveLookAndFeel neveLnf_;

    juce::ComboBox     psuModeBox_;
    juce::ToggleButton term600Btn_ { "600\xce\xa9 OUTPUT LOAD" };
    juce::Slider       xtalkKnob_, inTrimKnob_, outTrimKnob_;

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ComboAttach>  psuAttach_;
    std::unique_ptr<ButtonAttach> termAttach_;
    std::unique_ptr<SliderAttach> xtalkAttach_, inTrimAttach_, outTrimAttach_;

    float currentMa_ { 110.0f };
    float railVolts_ { 15.90f };
    float inLin_     { 0.0f };
    float outLin_    { 0.0f };
};

} // namespace lbx500::ui
