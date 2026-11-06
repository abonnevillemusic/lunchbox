#pragma once

#include <sdk500/Module500Interface.h>
#include "../DSP/Neve1073EQDSP.h"
#include <UI/NeveLookAndFeel.h>

#include <atomic>
#include <cmath>
#include <functional>

namespace nv73eq
{

/**
 * NV-73EQ: Vertical 500-Series Lunchbox adaptation of the Neve 1073LBEQ 3-Band Inductor EQ + HPF.
 *   - Draws 50 mA idle (+/-16V VPR rails) + up to 18 mA dynamic Class-A / inductor boost demand
 *   - Electronically balanced 10 kOhm bridging input, 75 Ohm output stage
 *   - Exports signed T1295/T1280/BA182C stray magnetic flux to adjacent 500-series slots
 */
class NV73EQModule500 final : public sdk500::Module500
{
public:
    struct AtomicState
    {
        std::atomic<float> hfGainDb     { 0.0f };
        std::atomic<int>   midFreqStep  { 3 };     // Default: 1.6 kHz
        std::atomic<float> midGainDb    { 0.0f };
        std::atomic<int>   lowFreqStep  { 2 };     // Default: 60 Hz
        std::atomic<float> lowGainDb    { 0.0f };
        std::atomic<int>   hpfStep      { 0 };     // Default: OFF
        std::atomic<float> outputTrimDb { 0.0f };
        std::atomic<bool>  eqEnabled    { true };
        std::atomic<bool>  phaseInvert  { false };
    };

    static const sdk500::ModuleDescriptor& staticDescriptor()
    {
        static const sdk500::ModuleDescriptor desc {
            "nv73eq",
            "NV-73EQ Inductor EQ",
            "NV-73EQ",
            "Equalizer",
            50.0 // 1073LBEQ hardware draws ~50 mA idle on +16V / 40 mA on -16V
        };
        return desc;
    }

    const sdk500::ModuleDescriptor& getDescriptor() const noexcept override
    {
        return staticDescriptor();
    }

    void prepare (double sampleRate, int /*maxBlockSize*/) override
    {
        dsp_.prepare (sampleRate);
        syncDspParams (true);
    }

    void reset() override
    {
        dsp_.reset();
    }

    void processSampleWithBackplane (float& left,
                                     float& right,
                                     int numChannels,
                                     const sdk500::SlotPowerContext& ctx,
                                     sdk500::SlotTelemetry& telemetryOut) noexcept override
    {
        if ((paramPollCounter_++ & 31) == 0)
            syncDspParams (false);

        double strayField = 0.0;
        double dynMa      = 0.0;

        dsp_.processFrameBackplane (left,
                                    right,
                                    numChannels,
                                    ctx.railVoltageScale,
                                    ctx.psuRippleVolts,
                                    ctx.inducedCrosstalkSample,
                                    strayField,
                                    dynMa);

        telemetryOut.idleCurrentMa       = 50.0;
        telemetryOut.dynamicCurrentMa    = dynMa;
        telemetryOut.inputImpedanceOhms  = 10000.0; // 10 kOhm balanced bridging input
        telemetryOut.outputImpedanceOhms = 75.0;    // Low-Z Class-A output buffer
        telemetryOut.strayMagneticFlux   = strayField;
    }

    std::unique_ptr<juce::Component> createSlotEditor() override;

    std::unique_ptr<juce::XmlElement> saveStateXml() const override
    {
        auto xml = std::make_unique<juce::XmlElement> ("NV73EQ");
        xml->setAttribute ("hfGain",      static_cast<double> (state_.hfGainDb.load()));
        xml->setAttribute ("midFreqStep", state_.midFreqStep.load());
        xml->setAttribute ("midGain",     static_cast<double> (state_.midGainDb.load()));
        xml->setAttribute ("lowFreqStep", state_.lowFreqStep.load());
        xml->setAttribute ("lowGain",     static_cast<double> (state_.lowGainDb.load()));
        xml->setAttribute ("hpfStep",     state_.hpfStep.load());
        xml->setAttribute ("outputTrim",  static_cast<double> (state_.outputTrimDb.load()));
        xml->setAttribute ("eqEnabled",   state_.eqEnabled.load());
        xml->setAttribute ("phaseInvert", state_.phaseInvert.load());
        return xml;
    }

    void loadStateXml (const juce::XmlElement& xml) override
    {
        state_.hfGainDb.store     (static_cast<float> (xml.getDoubleAttribute ("hfGain",      0.0)));
        state_.midFreqStep.store  (xml.getIntAttribute                        ("midFreqStep", 3));
        state_.midGainDb.store    (static_cast<float> (xml.getDoubleAttribute ("midGain",     0.0)));
        state_.lowFreqStep.store  (xml.getIntAttribute                        ("lowFreqStep", 2));
        state_.lowGainDb.store    (static_cast<float> (xml.getDoubleAttribute ("lowGain",     0.0)));
        state_.hpfStep.store      (xml.getIntAttribute                        ("hpfStep",     0));
        state_.outputTrimDb.store (static_cast<float> (xml.getDoubleAttribute ("outputTrim",  0.0)));
        state_.eqEnabled.store    (xml.getBoolAttribute                       ("eqEnabled",   true));
        state_.phaseInvert.store  (xml.getBoolAttribute                       ("phaseInvert", false));
        syncDspParams (true);
    }

    AtomicState& getState() noexcept { return state_; }
    const dsp::Neve1073EQDSP& getDSP() const noexcept { return dsp_; }

    dsp::EQParams buildCurrentParams() const noexcept
    {
        dsp::EQParams p;
        p.hfGainDb     = static_cast<double> (state_.hfGainDb.load (std::memory_order_relaxed));
        p.midFreqStep  = state_.midFreqStep.load (std::memory_order_relaxed);
        p.midGainDb    = static_cast<double> (state_.midGainDb.load (std::memory_order_relaxed));
        p.lowFreqStep  = state_.lowFreqStep.load (std::memory_order_relaxed);
        p.lowGainDb    = static_cast<double> (state_.lowGainDb.load (std::memory_order_relaxed));
        p.hpfStep      = state_.hpfStep.load (std::memory_order_relaxed);
        p.outputTrimDb = static_cast<double> (state_.outputTrimDb.load (std::memory_order_relaxed));
        p.eqEnabled    = state_.eqEnabled.load (std::memory_order_relaxed);
        p.phaseInvert  = state_.phaseInvert.load (std::memory_order_relaxed);
        p.oversampling = nv73::dsp::Oversampler::Factor::x4;
        return p;
    }

    void syncDspParams (bool snap) noexcept
    {
        dsp_.setParameters (buildCurrentParams(), 1.0, snap);
    }

private:
    AtomicState state_;
    dsp::Neve1073EQDSP dsp_;
    std::uint32_t paramPollCounter_ { 0 };
};

/**
 * Vertical 1.5" x 5.25" 500-Series Strip Editor for NV-73EQ (~150 x 428 px).
 * Features authentic dual-concentric-inspired side-by-side Marconi boost/cut knobs + frequency
 * switch selectors, live miniature EQ + HPF curve display, and inductor core glow lamp.
 */
class NV73EQVerticalStripUI final : public juce::Component,
                                    private juce::Timer
{
public:
    explicit NV73EQVerticalStripUI (NV73EQModule500& mod)
        : mod_ (mod)
    {
        setLookAndFeel (&lnf_);

        // 1. HF 12 kHz Shelf (-16 .. +16 dB)
        setupRotaryKnob (hfKnob_, -16.0, 16.0, 0.1, false, [this] (float v)
        {
            mod_.getState().hfGainDb.store (v);
            mod_.syncDspParams (false);
            repaint();
        });

        // 2. B211 Inductor Mid Bell (-18 .. +18 dB) + 7-position stepped tap selector
        setupRotaryKnob (midKnob_, -18.0, 18.0, 0.1, false, [this] (float v)
        {
            mod_.getState().midGainDb.store (v);
            mod_.syncDspParams (false);
            repaint();
        });
        const char* midLabels[7] = { "MF OFF", "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" };
        for (int i = 0; i < 7; ++i)
            midFreqBox_.addItem (midLabels[i], i + 1);
        midFreqBox_.onChange = [this]
        {
            mod_.getState().midFreqStep.store ( std::max (0, midFreqBox_.getSelectedId() - 1) );
            mod_.syncDspParams (false);
            repaint();
        };
        addAndMakeVisible (midFreqBox_);

        // 3. B205 Low Shelf (-16 .. +16 dB) + 5-position stepped polystyrene selector
        setupRotaryKnob (lowKnob_, -16.0, 16.0, 0.1, false, [this] (float v)
        {
            mod_.getState().lowGainDb.store (v);
            mod_.syncDspParams (false);
            repaint();
        });
        const char* lowLabels[5] = { "LF OFF", "35 Hz", "60 Hz", "110 Hz", "220 Hz" };
        for (int i = 0; i < 5; ++i)
            lowFreqBox_.addItem (lowLabels[i], i + 1);
        lowFreqBox_.onChange = [this]
        {
            mod_.getState().lowFreqStep.store ( std::max (0, lowFreqBox_.getSelectedId() - 1) );
            mod_.syncDspParams (false);
            repaint();
        };
        addAndMakeVisible (lowFreqBox_);

        // 4. BA182C 18 dB/oct Inductor HPF + Red Marconi Output Trim (-18 .. +18 dB)
        setupRotaryKnob (trimKnob_, -18.0, 18.0, 0.1, true, [this] (float v)
        {
            mod_.getState().outputTrimDb.store (v);
            mod_.syncDspParams (false);
            repaint();
        });
        const char* hpfLabels[5] = { "HPF OFF", "50 Hz", "80 Hz", "160 Hz", "300 Hz" };
        for (int i = 0; i < 5; ++i)
            hpfBox_.addItem (hpfLabels[i], i + 1);
        hpfBox_.onChange = [this]
        {
            mod_.getState().hpfStep.store ( std::max (0, hpfBox_.getSelectedId() - 1) );
            mod_.syncDspParams (false);
            repaint();
        };
        addAndMakeVisible (hpfBox_);

        // 5. EQL (EQ In) & Phase Invert Pushbuttons
        eqInBtn_.onClick = [this]
        {
            mod_.getState().eqEnabled.store (eqInBtn_.getToggleState());
            mod_.syncDspParams (false);
            repaint();
        };
        addAndMakeVisible (eqInBtn_);

        phaseBtn_.onClick = [this]
        {
            mod_.getState().phaseInvert.store (phaseBtn_.getToggleState());
            mod_.syncDspParams (false);
            repaint();
        };
        addAndMakeVisible (phaseBtn_);

        syncControlsFromState();
        startTimerHz (24);
    }

    ~NV73EQVerticalStripUI() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        using nv73::ui::NeveLookAndFeel;
        const auto b = getLocalBounds().toFloat();

        // RAF Blue-Grey vertical anodized 500-series faceplate
        juce::ColourGradient grad (NeveLookAndFeel::kFaceplateBase.brighter (0.05f), 0.0f, 0.0f,
                                   NeveLookAndFeel::kFaceplateBase.darker (0.18f), b.getWidth(), b.getHeight(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, 3.0f);

        g.setColour (NeveLookAndFeel::kRailSilver.withAlpha (0.35f));
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

        // Top & Bottom VPR #4-40 Knurled Thumbscrews
        drawThumbscrew (g, b.getCentreX(), 10.0f);
        drawThumbscrew (g, b.getCentreX(), b.getBottom() - 10.0f);

        // Module Nameplate + Inductor Glow Lamp
        g.setColour (NeveLookAndFeel::kCreamLegend);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("NV-73EQ", 8, 19, getWidth() - 32, 16, juce::Justification::centredLeft);

        const juce::Point<float> ledPos (b.getWidth() - 16.0f, 27.0f);
        const auto ledCol = juce::Colour (0xff3a2a18).interpolatedWith (juce::Colour (0xffff922b), std::sqrt (glowLevel_));
        g.setColour (ledCol);
        g.fillEllipse (ledPos.x - 4.5f, ledPos.y - 4.5f, 9.0f, 9.0f);

        // Subtle horizontal dividers between bands
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        for (float yDiv : { 38.0f, 98.0f, 162.0f, 226.0f, 294.0f })
            g.drawHorizontalLine (static_cast<int> (yDiv), 6.0f, b.getWidth() - 6.0f);

        // Band legends & live dB readouts on right side of each Marconi knob
        const auto p = mod_.buildCurrentParams();
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));

        g.setColour (NeveLookAndFeel::kCreamLegend);
        g.drawText ("HF 12 kHz", 66, 46, 78, 13, juce::Justification::centredLeft);
        g.setColour (NeveLookAndFeel::kAmberGlow);
        g.drawText (formatSignedDb (p.hfGainDb), 66, 61, 78, 14, juce::Justification::centredLeft);

        g.setColour (NeveLookAndFeel::kCreamLegend);
        g.drawText ("MF " + formatSignedDb (p.midGainDb), 66, 102, 78, 14, juce::Justification::centredLeft);

        g.setColour (NeveLookAndFeel::kCreamLegend);
        g.drawText ("LF " + formatSignedDb (p.lowGainDb), 66, 166, 78, 14, juce::Justification::centredLeft);

        g.setColour (NeveLookAndFeel::kCreamLegend);
        g.drawText ("TRIM " + formatSignedDb (p.outputTrimDb), 66, 232, 78, 14, juce::Justification::centredLeft);

        // Live Miniature EQ + HPF Curve Display (y = 298 .. 354)
        drawMiniEqCurve (g, juce::Rectangle<float> (7.0f, 299.0f, b.getWidth() - 14.0f, 54.0f), p);
    }

    void resized() override
    {
        const int w = getWidth();
        hfKnob_.setBounds   (5, 40,  58, 56);

        midKnob_.setBounds    (5,  102, 58, 56);
        midFreqBox_.setBounds (65, 120, w - 71, 22);

        lowKnob_.setBounds    (5,  166, 58, 56);
        lowFreqBox_.setBounds (65, 184, w - 71, 22);

        trimKnob_.setBounds (5,  232, 58, 56);
        hpfBox_.setBounds   (65, 250, w - 71, 22);

        const int btnY = 360;
        eqInBtn_.setBounds  (6,      btnY, w - 58, 24);
        phaseBtn_.setBounds (w - 48, btnY, 42,     24);
    }

private:
    void setupRotaryKnob (juce::Slider& s, double lo, double hi, double step, bool isRed, std::function<void (float)> cb)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRange (lo, hi, step);
        s.setDoubleClickReturnValue (true, 0.0);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.getProperties().set ("marconiRed", isRed);
        s.onValueChange = [cb, &s] { cb (static_cast<float> (s.getValue())); };
        addAndMakeVisible (s);
    }

    void syncControlsFromState()
    {
        const auto& st = mod_.getState();
        hfKnob_.setValue   (st.hfGainDb.load(),     juce::dontSendNotification);
        midKnob_.setValue  (st.midGainDb.load(),    juce::dontSendNotification);
        lowKnob_.setValue  (st.lowGainDb.load(),    juce::dontSendNotification);
        trimKnob_.setValue (st.outputTrimDb.load(), juce::dontSendNotification);

        midFreqBox_.setSelectedId (st.midFreqStep.load() + 1, juce::dontSendNotification);
        lowFreqBox_.setSelectedId (st.lowFreqStep.load() + 1, juce::dontSendNotification);
        hpfBox_.setSelectedId     (st.hpfStep.load()     + 1, juce::dontSendNotification);

        eqInBtn_.setToggleState  (st.eqEnabled.load(),   juce::dontSendNotification);
        phaseBtn_.setToggleState (st.phaseInvert.load(), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const float newGlow = mod_.getDSP().getInductorGlow();
        if (std::abs (newGlow - glowLevel_) > 0.005f)
        {
            glowLevel_ = newGlow;
            repaint (getWidth() - 32, 16, 26, 22);
        }
    }

    static juce::String formatSignedDb (double db)
    {
        return (db >= 0.0 ? "+" : "") + juce::String (db, 1) + "dB";
    }

    static void drawMiniEqCurve (juce::Graphics& g, juce::Rectangle<float> area, const dsp::EQParams& p)
    {
        g.setColour (juce::Colour (0xff12171c));
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (juce::Colour (0xff3a4652));
        g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);

        // 0 dB centerline
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine (static_cast<int> (area.getCentreY()), area.getX() + 2.0f, area.getRight() - 2.0f);

        juce::Path curve;
        const int numPts = 64;
        for (int i = 0; i <= numPts; ++i)
        {
            const double normX = static_cast<double> (i) / static_cast<double> (numPts);
            const double freq  = 20.0 * std::pow (1100.0, normX); // 20 Hz .. 22 kHz
            const double db    = std::clamp (dsp::Neve1073EQDSP::evaluateCompositeMagDb (freq, p), -20.0, 20.0);
            const float px = area.getX() + 2.0f + static_cast<float> (normX) * (area.getWidth() - 4.0f);
            const float py = area.getCentreY() - static_cast<float> (db / 22.0) * (area.getHeight() * 0.46f);
            if (i == 0)
                curve.startNewSubPath (px, py);
            else
                curve.lineTo (px, py);
        }

        g.setColour (p.eqEnabled ? juce::Colour (0xfff5a623) : juce::Colour (0xff7c8a96));
        g.strokePath (curve, juce::PathStrokeType (1.7f));
    }

    static void drawThumbscrew (juce::Graphics& g, float cx, float cy)
    {
        g.setColour (juce::Colour (0xff151a1f));
        g.fillEllipse (cx - 5.5f, cy - 5.5f, 11.0f, 11.0f);
        g.setColour (juce::Colour (0xffb8c1c8));
        g.fillEllipse (cx - 4.2f, cy - 4.2f, 8.4f, 8.4f);
        g.setColour (juce::Colour (0xff2c343b));
        g.drawLine (cx - 3.0f, cy, cx + 3.0f, cy, 1.4f);
    }

    NV73EQModule500& mod_;
    nv73::ui::NeveLookAndFeel lnf_;

    juce::Slider   hfKnob_, midKnob_, lowKnob_, trimKnob_;
    juce::ComboBox midFreqBox_, lowFreqBox_, hpfBox_;
    juce::ToggleButton eqInBtn_  { "EQL" };
    juce::ToggleButton phaseBtn_ { "\xc3\x98" };

    float glowLevel_ { 0.0f };
};

inline std::unique_ptr<juce::Component> NV73EQModule500::createSlotEditor()
{
    return std::make_unique<NV73EQVerticalStripUI> (*this);
}

} // namespace nv73eq
