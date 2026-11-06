#pragma once

#include "../Module500Interface.h"
#include <atomic>
#include <cmath>

namespace sdk500
{

/**
 * VP-600: Passive 600-Ohm Bridged-T Attenuator, 600-Ohm Transformer Termination Load, & Polarity Module.
 *   - 0 mA PSU rail current draw (100% passive resistor network!)
 *   - Lets engineers place a real 600-ohm load & interstage pad AFTER a hot NV-73LB (cooking its LO1166
 *     output transformer!) before feeding a second NV-73LB or downstream compressor/EQ in the Lunchbox!
 */
class VP600Module500 final : public Module500
{
public:
    static const ModuleDescriptor& staticDescriptor()
    {
        static const ModuleDescriptor desc {
            "vp600",
            "VP-600 Passive T-Pad / Load",
            "VP-600",
            "Utility / Pad",
            0.0 // Passive Bridged-T network draws 0 mA from +/-16V rails!
        };
        return desc;
    }

    const ModuleDescriptor& getDescriptor() const noexcept override { return staticDescriptor(); }

    void prepare (double sampleRate, int /*maxBlockSize*/) override
    {
        const double samples = std::max (1.0, 0.008 * sampleRate);
        smoothCoeff_ = 1.0 - std::exp (-1.0 / samples);
        currentGain_ = computeTargetLinear();
    }

    void reset() override
    {
        currentGain_ = computeTargetLinear();
    }

    void processSampleWithBackplane (float& left,
                                     float& right,
                                     int numChannels,
                                     const SlotPowerContext& /*ctx*/,
                                     SlotTelemetry& telemetryOut) noexcept override
    {
        const double target = computeTargetLinear();
        currentGain_ += smoothCoeff_ * (target - currentGain_);

        const float g = static_cast<float> (currentGain_) * (phaseInvert_.load (std::memory_order_relaxed) ? -1.0f : 1.0f);
        left *= g;
        if (numChannels > 1)
            right *= g;
        else
            right = left;

        const bool load600 = terminate600_.load (std::memory_order_relaxed);
        telemetryOut.idleCurrentMa       = 0.0;
        telemetryOut.dynamicCurrentMa    = 0.0;
        telemetryOut.inputImpedanceOhms  = load600 ? 600.0 : 10000.0;
        telemetryOut.outputImpedanceOhms = 600.0;
        telemetryOut.strayMagneticFlux   = 0.0;
    }

    std::unique_ptr<juce::Component> createSlotEditor() override;

    std::unique_ptr<juce::XmlElement> saveStateXml() const override
    {
        auto xml = std::make_unique<juce::XmlElement> ("VP600");
        xml->setAttribute ("attenuationDb", static_cast<double> (attenuationDb_.load()));
        xml->setAttribute ("terminate600",  terminate600_.load());
        xml->setAttribute ("phaseInvert",   phaseInvert_.load());
        return xml;
    }

    void loadStateXml (const juce::XmlElement& xml) override
    {
        attenuationDb_.store (static_cast<float> (xml.getDoubleAttribute ("attenuationDb", -6.0)));
        terminate600_.store  (xml.getBoolAttribute ("terminate600", true));
        phaseInvert_.store   (xml.getBoolAttribute ("phaseInvert", false));
    }

    std::atomic<float>& attenuationDb() noexcept { return attenuationDb_; }
    std::atomic<bool>&  terminate600()  noexcept { return terminate600_; }
    std::atomic<bool>&  phaseInvert()   noexcept { return phaseInvert_; }

private:
    double computeTargetLinear() const noexcept
    {
        const double db = static_cast<double> (attenuationDb_.load (std::memory_order_relaxed));
        return std::pow (10.0, db / 20.0);
    }

    std::atomic<float> attenuationDb_ { -6.0f }; // -36 dB .. 0 dB passive Bridged-T pad
    std::atomic<bool>  terminate600_  { true };  // True = 600 ohm classic load on preceding slot's transformer!
    std::atomic<bool>  phaseInvert_   { false };

    double smoothCoeff_ { 0.01 };
    double currentGain_ { 0.5 };
};

class VP600VerticalStripUI final : public juce::Component
{
public:
    explicit VP600VerticalStripUI (VP600Module500& mod) : mod_ (mod)
    {
        padSlider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        padSlider_.setRange (-36.0, 0.0, 0.5);
        padSlider_.setTextValueSuffix (" dB");
        padSlider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        padSlider_.setValue (mod_.attenuationDb().load(), juce::dontSendNotification);
        padSlider_.onValueChange = [this]
        {
            mod_.attenuationDb().store (static_cast<float> (padSlider_.getValue()));
        };
        addAndMakeVisible (padSlider_);

        termBtn_.setToggleState (mod_.terminate600().load(), juce::dontSendNotification);
        termBtn_.onClick = [this] { mod_.terminate600().store (termBtn_.getToggleState()); };
        addAndMakeVisible (termBtn_);

        phaseBtn_.setToggleState (mod_.phaseInvert().load(), juce::dontSendNotification);
        phaseBtn_.onClick = [this] { mod_.phaseInvert().store (phaseBtn_.getToggleState()); };
        addAndMakeVisible (phaseBtn_);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        juce::ColourGradient grad (juce::Colour (0xff25272b), 0.0f, 0.0f,
                                   juce::Colour (0xff17191c), b.getWidth(), b.getHeight(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (juce::Colour (0xff525862));
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

        // Top & bottom VPR thumbscrews
        for (float cy : { 11.0f, b.getBottom() - 11.0f })
        {
            g.setColour (juce::Colour (0xffb8c1c8));
            g.fillEllipse (b.getCentreX() - 4.2f, cy - 4.2f, 8.4f, 8.4f);
            g.setColour (juce::Colour (0xff2c343b));
            g.drawLine (b.getCentreX() - 3.0f, cy, b.getCentreX() + 3.0f, cy, 1.4f);
        }

        g.setColour (juce::Colour (0xffe8e4d8));
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText ("VP-600 PAD", 0, 24, getWidth(), 18, juce::Justification::centred);

        g.setColour (juce::Colour (0xff9aa3ad));
        g.setFont (juce::FontOptions (10.0f, juce::Font::plain));
        g.drawText ("PASSIVE BRIDGED-T", 0, 42, getWidth(), 14, juce::Justification::centred);
        g.drawText ("0 mA RAIL DRAW",    0, 392, getWidth(), 14, juce::Justification::centred);

        g.setColour (juce::Colour (0xffe8e4d8));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("ATTENUATION", 0, 88, getWidth(), 16, juce::Justification::centred);
    }

    void resized() override
    {
        const int w = getWidth();
        padSlider_.setBounds ((w - 96) / 2, 108, 96, 114);
        termBtn_.setBounds   (12, 260, w - 24, 30);
        phaseBtn_.setBounds  (12, 302, w - 24, 30);
    }

private:
    VP600Module500& mod_;
    juce::Slider padSlider_;
    juce::ToggleButton termBtn_  { "600\xce\xa9 LOAD" };
    juce::ToggleButton phaseBtn_ { "PHASE \xc3\x98" };
};

inline std::unique_ptr<juce::Component> VP600Module500::createSlotEditor()
{
    return std::make_unique<VP600VerticalStripUI> (*this);
}

} // namespace sdk500
