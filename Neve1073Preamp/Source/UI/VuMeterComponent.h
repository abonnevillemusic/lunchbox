#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "NeveLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace nv73::ui
{

/**
 * Dual Input/Output Console Meter + Class-A / LO1166 Iron Harmonic Saturation Lamp.
 */
class VuMeterComponent final : public juce::Component
{
public:
    void setLevels (float inLinear, float outLinear, float saturationNorm, bool threeStageActive) noexcept
    {
        inDb_         = linearToDbClamped (inLinear);
        outDb_        = linearToDbClamped (outLinear);
        saturation_   = std::clamp (saturationNorm, 0.0f, 1.0f);
        threeStage_   = threeStageActive;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        // Warm backlit recessed meter housing
        g.setColour (juce::Colour (0xff1b2127));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (juce::Colour (0xff4e5b68));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.2f);

        auto inner = bounds.reduced (10.0f, 8.0f);

        // Top row: Topology badge (2-STAGE BA283 vs 3-STAGE BA284+BA283) + Class-A Saturation Lamp
        auto topRow = inner.removeFromTop (22.0f);

        auto lampArea = topRow.removeFromRight (120.0f);
        drawSaturationLamp (g, lampArea);

        g.setColour (threeStage_ ? NeveLookAndFeel::kAmberGlow : NeveLookAndFeel::kCreamLegend.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (threeStage_ ? "TOPOLOGY: 3-STAGE [BA284 + BA283]" : "TOPOLOGY: 2-STAGE [BA283]",
                    topRow.toNearestInt(),
                    juce::Justification::centredLeft);

        inner.removeFromTop (6.0f);

        // Input & Output horizontal LED/bar meters (-48 dBFS .. +6 dBFS, 0 VU marker at -18 dBFS)
        auto inRow = inner.removeFromTop (18.0f);
        inner.removeFromTop (6.0f);
        auto outRow = inner.removeFromTop (18.0f);

        drawBar (g, inRow,  "IN",  inDb_);
        drawBar (g, outRow, "OUT", outDb_);
    }

private:
    static float linearToDbClamped (float lin) noexcept
    {
        if (lin <= 1e-5f)
            return -60.0f;
        return std::clamp (20.0f * std::log10 (lin), -60.0f, 6.0f);
    }

    static float dbToNorm (float db) noexcept
    {
        return std::clamp ((db + 48.0f) / 54.0f, 0.0f, 1.0f); // [-48 .. +6 dB] -> [0 .. 1]
    }

    void drawSaturationLamp (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const float r = 6.0f;
        const juce::Point<float> c (area.getRight() - 10.0f, area.getCentreY());

        // Glow halo proportional to Class-A + Transformer core harmonic drive
        if (saturation_ > 0.02f)
        {
            juce::Colour glowCol = juce::Colour (0xfff59e0b).interpolatedWith (juce::Colour (0xffef4444),
                                                                               std::clamp (saturation_ * 1.2f, 0.0f, 1.0f));
            g.setColour (glowCol.withAlpha (0.35f * saturation_));
            g.fillEllipse (c.x - r * 2.1f, c.y - r * 2.1f, r * 4.2f, r * 4.2f);
        }

        const juce::Colour coreCol = juce::Colour (0xff3b2f24).interpolatedWith (
            juce::Colour (0xffff7b29), std::sqrt (saturation_));
        g.setColour (coreCol);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);

        g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("IRON / 2N3055", area.withTrimmedRight (22.0f).toNearestInt(), juce::Justification::centredRight);
    }

    void drawBar (juce::Graphics& g, juce::Rectangle<float> row, const char* label, float db) const
    {
        auto labelRect = row.removeFromLeft (32.0f);
        g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (label, labelRect.toNearestInt(), juce::Justification::centredLeft);

        auto track = row.reduced (0.0f, 3.0f);
        g.setColour (juce::Colour (0xff101418));
        g.fillRoundedRectangle (track, 3.0f);

        const float norm = dbToNorm (db);
        if (norm > 0.005f)
        {
            auto fillRect = track.withWidth (track.getWidth() * norm);
            juce::ColourGradient meterGrad (juce::Colour (0xff38b26f), track.getX(), track.getY(),
                                            juce::Colour (0xffeab308), track.getX() + track.getWidth() * 0.78f, track.getY(), false);
            meterGrad.addColour (0.94, juce::Colour (0xffef4444));
            g.setGradientFill (meterGrad);
            g.fillRoundedRectangle (fillRect, 3.0f);
        }

        // -18 dBFS (0 VU) & 0 dBFS calibration ticks
        for (float tickDb : { -18.0f, -6.0f, 0.0f })
        {
            const float tx = track.getX() + track.getWidth() * dbToNorm (tickDb);
            g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.45f));
            g.drawVerticalLine (static_cast<int> (std::round (tx)), track.getY() - 1.0f, track.getBottom() + 1.0f);
        }
    }

    float inDb_       { -60.0f };
    float outDb_      { -60.0f };
    float saturation_ { 0.0f };
    bool  threeStage_ { false };
};

} // namespace nv73::ui
