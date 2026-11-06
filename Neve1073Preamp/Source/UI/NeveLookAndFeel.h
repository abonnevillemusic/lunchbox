#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace nv73::ui
{

/**
 * Vintage 1970s British Console LookAndFeel inspired by the classic RAF Blue-Grey Neve 1073/1290:
 *   - Powder-coated RAF Blue-Grey faceplate (#35404B)
 *   - Iconic Marconi skirted wing knobs (Burgundy Red for PL20033 Mic Gain; Warm Slate Grey for Fader/Trim)
 *   - Engraved cream console legends (#EFEBD8) and illuminated rectangular pushbuttons
 */
class NeveLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour kFaceplateBase  { 0xff343f4a };
    static inline const juce::Colour kFaceplateDark  { 0xff262e36 };
    static inline const juce::Colour kRailSilver     { 0xff9aa3ab };
    static inline const juce::Colour kCreamLegend    { 0xffefebd8 };
    static inline const juce::Colour kMarconiRed     { 0xffb3292b };
    static inline const juce::Colour kMarconiRedHi   { 0xffd63d3f };
    static inline const juce::Colour kMarconiGrey    { 0xff626c75 };
    static inline const juce::Colour kAmberGlow      { 0xfff5a623 };

    NeveLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId,       kCreamLegend);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff1f262d));
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colour (0xff4d5a66));
    }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const bool isRedGainKnob = slider.getProperties().getWithDefault ("marconiRed", false);

        const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                    static_cast<float> (y),
                                                    static_cast<float> (width),
                                                    static_cast<float> (height)).reduced (8.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre  = bounds.getCentre();
        const float angle  = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // 1. Engraved console dial ticks around the skirt
        const int numTicks = isRedGainKnob ? 14 : 11;
        for (int i = 0; i < numTicks; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (numTicks - 1);
            const float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle) - juce::MathConstants<float>::halfPi;
            const bool isTransitionStep = isRedGainKnob && (i == 8); // 55 dB 3-stage boundary highlight!

            const float rInner = radius * 0.84f;
            const float rOuter = radius * (isTransitionStep ? 1.00f : 0.95f);

            g.setColour (isTransitionStep ? kAmberGlow : kCreamLegend.withAlpha (0.82f));
            g.drawLine (centre.x + std::cos (a) * rInner,
                        centre.y + std::sin (a) * rInner,
                        centre.x + std::cos (a) * rOuter,
                        centre.y + std::sin (a) * rOuter,
                        isTransitionStep ? 2.4f : 1.5f);
        }

        const float skirtR = radius * 0.76f;
        const float bodyR  = radius * 0.58f;

        // 2. Soft drop shadow beneath Marconi skirt
        g.setColour (juce::Colours::black.withAlpha (0.48f));
        g.fillEllipse (centre.x - skirtR + 2.0f, centre.y - skirtR + 4.0f, skirtR * 2.0f, skirtR * 2.0f);

        // 3. Flared Marconi skirt with brushed metallic bevel
        const juce::Colour baseCol = isRedGainKnob ? kMarconiRed   : kMarconiGrey;
        const juce::Colour hiCol   = isRedGainKnob ? kMarconiRedHi : juce::Colour (0xff818c96);

        juce::ColourGradient skirtGrad (hiCol.darker (0.2f), centre.x - skirtR, centre.y - skirtR,
                                        baseCol.darker (0.55f), centre.x + skirtR, centre.y + skirtR, false);
        g.setGradientFill (skirtGrad);
        g.fillEllipse (centre.x - skirtR, centre.y - skirtR, skirtR * 2.0f, skirtR * 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawEllipse (centre.x - skirtR, centre.y - skirtR, skirtR * 2.0f, skirtR * 2.0f, 1.2f);

        // 4. Raised inner cylinder + brushed silver cap insert
        juce::ColourGradient capGrad (hiCol, centre.x, centre.y - bodyR,
                                      baseCol.darker (0.35f), centre.x, centre.y + bodyR, false);
        g.setGradientFill (capGrad);
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        // 5. Raised Marconi wing bar rotating with angle
        {
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));

            const float wingHalfW = bodyR * 0.24f;
            juce::Rectangle<float> wing (-wingHalfW, -bodyR * 1.02f, wingHalfW * 2.0f, bodyR * 1.92f);
            wing.translate (centre.x, centre.y);

            g.setColour (juce::Colours::black.withAlpha (0.30f));
            g.fillRoundedRectangle (wing.translated (1.5f, 2.0f), 3.0f);

            g.setColour (hiCol.brighter (0.08f));
            g.fillRoundedRectangle (wing, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.24f));
            g.drawRoundedRectangle (wing, 3.0f, 1.0f);

            // Crisp cream pointer line on top nose of Marconi wing
            juce::Rectangle<float> pointer (centre.x - 1.6f, centre.y - skirtR * 0.96f, 3.2f, skirtR * 0.52f);
            g.setColour (kCreamLegend);
            g.fillRoundedRectangle (pointer, 1.2f);
        }
    }

    void drawToggleButton (juce::Graphics& g,
                           juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool /*shouldDrawButtonAsDown*/) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
        const bool isOn   = button.getToggleState();

        // Recessed console bezel
        g.setColour (juce::Colour (0xff192026));
        g.fillRoundedRectangle (bounds, 4.0f);

        const auto cap = bounds.reduced (2.5f);
        const juce::Colour onCol  = juce::Colour (0xffd88c27); // Warm vintage incandescent amber
        const juce::Colour offCol = juce::Colour (0xff45525e);

        juce::Colour fill = isOn ? onCol : offCol;
        if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter (0.12f);

        juce::ColourGradient grad (fill.brighter (0.18f), cap.getX(), cap.getY(),
                                   fill.darker (0.28f),   cap.getX(), cap.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (cap, 3.0f);

        g.setColour (isOn ? juce::Colours::white.withAlpha (0.45f) : juce::Colours::white.withAlpha (0.14f));
        g.drawRoundedRectangle (cap, 3.0f, 1.0f);

        g.setColour (isOn ? juce::Colour (0xff1a1408) : kCreamLegend);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawFittedText (button.getButtonText(), cap.toNearestInt(), juce::Justification::centred, 1);
    }
};

} // namespace nv73::ui
