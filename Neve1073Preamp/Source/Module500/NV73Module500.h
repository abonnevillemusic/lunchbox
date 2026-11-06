#pragma once

#include <sdk500/Module500Interface.h>
#include "../DSP/Neve1073PreampDSP.h"
#include "../UI/NeveLookAndFeel.h"

#include <atomic>
#include <cmath>

namespace nv73
{

/**
 * NV-73LB: Vertical 500-Series Lunchbox adaptation of the Neve 1073 Class-A Mic Preamp.
 *   - Draws 110 mA idle (+/-16V VPR rails via internal DC-DC converter) + up to 42 mA dynamic peak
 *   - Input Z switches dynamically between 1200 ohm (Hi-Z), 300 ohm (Lo-Z), 910/260 ohm (3-stage), 10k (Line)
 *   - Output Z = 75 ohm (LO1166 secondary)
 *   - Exports signed T10468/LO1166 stray magnetic field to adjacent 500-series slots
 */
class NV73Module500 final : public sdk500::Module500
{
public:
    struct AtomicState
    {
        std::atomic<int>   gainStepIndex { 3 };     // Default: 30 dB
        std::atomic<float> faderDriveDb  { 0.0f };
        std::atomic<float> outputTrimDb  { 0.0f };
        std::atomic<float> biasTrimPct   { 0.0f };
        std::atomic<bool>  loZ           { false };
        std::atomic<bool>  phaseInvert   { false };
        std::atomic<bool>  autoGain      { true };
        std::atomic<bool>  analogNoise   { false };
    };

    static const sdk500::ModuleDescriptor& staticDescriptor()
    {
        static const sdk500::ModuleDescriptor desc {
            "nv73lb",
            "NV-73LB Class-A Preamp",
            "NV-73LB",
            "Mic Preamp",
            110.0 // Classic 1073LB draws ~110 mA idle on +/-16V rails
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
        double inZOhms    = 1200.0;

        dsp_.processFrameBackplane (left,
                                    right,
                                    numChannels,
                                    ctx.railVoltageScale,
                                    ctx.psuRippleVolts,
                                    ctx.inducedCrosstalkSample,
                                    strayField,
                                    dynMa,
                                    inZOhms);

        telemetryOut.idleCurrentMa       = 110.0;
        telemetryOut.dynamicCurrentMa    = dynMa;
        telemetryOut.inputImpedanceOhms  = inZOhms;
        telemetryOut.outputImpedanceOhms = 75.0; // Marinair LO1166 secondary impedance
        telemetryOut.strayMagneticFlux   = strayField;
    }

    std::unique_ptr<juce::Component> createSlotEditor() override;

    std::unique_ptr<juce::XmlElement> saveStateXml() const override
    {
        auto xml = std::make_unique<juce::XmlElement> ("NV73LB");
        xml->setAttribute ("gainStep",    state_.gainStepIndex.load());
        xml->setAttribute ("faderDrive",  static_cast<double> (state_.faderDriveDb.load()));
        xml->setAttribute ("outputTrim",  static_cast<double> (state_.outputTrimDb.load()));
        xml->setAttribute ("biasTrim",    static_cast<double> (state_.biasTrimPct.load()));
        xml->setAttribute ("loZ",         state_.loZ.load());
        xml->setAttribute ("phaseInvert", state_.phaseInvert.load());
        xml->setAttribute ("autoGain",    state_.autoGain.load());
        xml->setAttribute ("analogNoise", state_.analogNoise.load());
        return xml;
    }

    void loadStateXml (const juce::XmlElement& xml) override
    {
        state_.gainStepIndex.store (xml.getIntAttribute ("gainStep", 3));
        state_.faderDriveDb.store  (static_cast<float> (xml.getDoubleAttribute ("faderDrive", 0.0)));
        state_.outputTrimDb.store  (static_cast<float> (xml.getDoubleAttribute ("outputTrim", 0.0)));
        state_.biasTrimPct.store   (static_cast<float> (xml.getDoubleAttribute ("biasTrim", 0.0)));
        state_.loZ.store           (xml.getBoolAttribute ("loZ", false));
        state_.phaseInvert.store   (xml.getBoolAttribute ("phaseInvert", false));
        state_.autoGain.store      (xml.getBoolAttribute ("autoGain", true));
        state_.analogNoise.store   (xml.getBoolAttribute ("analogNoise", false));
        syncDspParams (true);
    }

    AtomicState& getState() noexcept { return state_; }
    const dsp::Neve1073PreampDSP& getDSP() const noexcept { return dsp_; }

    void syncDspParams (bool snap) noexcept
    {
        dsp::PreampParams p;
        p.gainStepIndex = state_.gainStepIndex.load (std::memory_order_relaxed);
        p.faderDriveDb  = static_cast<double> (state_.faderDriveDb.load (std::memory_order_relaxed));
        p.outputTrimDb  = static_cast<double> (state_.outputTrimDb.load (std::memory_order_relaxed));
        p.biasTrim      = static_cast<double> (state_.biasTrimPct.load (std::memory_order_relaxed)) * 0.01;
        p.loZImpedance  = state_.loZ.load (std::memory_order_relaxed);
        p.phaseInvert   = state_.phaseInvert.load (std::memory_order_relaxed);
        p.autoGainComp  = state_.autoGain.load (std::memory_order_relaxed);
        p.analogNoise   = state_.analogNoise.load (std::memory_order_relaxed);
        p.oversampling  = dsp::Oversampler::Factor::x4;
        dsp_.setParameters (p, snap);
    }

private:
    AtomicState state_;
    dsp::Neve1073PreampDSP dsp_;
    std::uint32_t paramPollCounter_ { 0 };
};

/**
 * Vertical 1.5" x 5.25" 500-Series Strip Editor for NV-73LB (~148 x 436 px).
 */
class NV73VerticalStripUI final : public juce::Component,
                                  private juce::Timer
{
public:
    explicit NV73VerticalStripUI (NV73Module500& mod)
        : mod_ (mod)
    {
        setLookAndFeel (&lnf_);

        // 1. Stepped Red Marconi Gain Knob
        gainKnob_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        gainKnob_.setRange (0.0, 13.0, 1.0);
        gainKnob_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        gainKnob_.getProperties().set ("marconiRed", true);
        gainKnob_.onValueChange = [this]
        {
            const int idx = static_cast<int> (std::lround (gainKnob_.getValue()));
            mod_.getState().gainStepIndex.store (idx);
            mod_.syncDspParams (false);
            updateGainLabel();
        };
        addAndMakeVisible (gainKnob_);

        // 2. Grey Marconi Fader Drive Knob
        setupContinuousKnob (faderKnob_, -24.0, 12.0, 0.1, [this] (float v)
        {
            mod_.getState().faderDriveDb.store (v);
            mod_.syncDspParams (false);
        });

        // 3. Grey Marconi Output Trim Knob
        setupContinuousKnob (outKnob_, -36.0, 12.0, 0.1, [this] (float v)
        {
            mod_.getState().outputTrimDb.store (v);
            mod_.syncDspParams (false);
        });

        // 4. Compact RV1 Bias Trim Knob
        setupContinuousKnob (biasKnob_, -100.0, 100.0, 1.0, [this] (float v)
        {
            mod_.getState().biasTrimPct.store (v);
            mod_.syncDspParams (false);
        });
        biasKnob_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

        // 5. Pushbuttons
        setupButton (loZBtn_,  mod_.getState().loZ);
        setupButton (phaseBtn_, mod_.getState().phaseInvert);
        setupButton (autoBtn_,  mod_.getState().autoGain);
        setupButton (hissBtn_,  mod_.getState().analogNoise);

        syncControlsFromState();
        startTimerHz (24);
    }

    ~NV73VerticalStripUI() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace ui;
        const auto b = getLocalBounds().toFloat();

        // RAF Blue-Grey vertical anodized 500-series faceplate
        juce::ColourGradient grad (NeveLookAndFeel::kFaceplateBase.brighter (0.05f), 0.0f, 0.0f,
                                   NeveLookAndFeel::kFaceplateBase.darker (0.18f), b.getWidth(), b.getHeight(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, 3.0f);

        g.setColour (NeveLookAndFeel::kRailSilver.withAlpha (0.35f));
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

        // Top & Bottom VPR #4-40 Knurled Thumbscrews
        drawThumbscrew (g, b.getCentreX(), 11.0f);
        drawThumbscrew (g, b.getCentreX(), b.getBottom() - 11.0f);

        // Module Nameplate
        g.setColour (NeveLookAndFeel::kCreamLegend);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText ("NV-73LB", 0, 21, getWidth(), 16, juce::Justification::centred);

        // 2-Stage / 3-Stage indicator + stepped dB readout beneath Red Knob
        g.setColour (threeStage_ ? NeveLookAndFeel::kAmberGlow : NeveLookAndFeel::kCreamLegend.withAlpha (0.75f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (gainReadout_, 4, 128, getWidth() - 8, 14, juce::Justification::centred);

        g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.85f));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText ("FADER DRIVE", 0, 145, getWidth(), 12, juce::Justification::centred);
        g.drawText ("OUTPUT PAD",  0, 232, getWidth(), 12, juce::Justification::centred);
        g.drawText ("RV1",         8, 318, 38,         12, juce::Justification::centred);

        // 5-LED Signal & Iron Saturation Ladder (right of RV1 trim)
        drawMiniLedLadder (g, juce::Rectangle<float> (54.0f, 322.0f, b.getWidth() - 64.0f, 22.0f));
    }

    void resized() override
    {
        const int w = getWidth();
        gainKnob_.setBounds  ((w - 94) / 2, 36,  94, 94);
        faderKnob_.setBounds ((w - 76) / 2, 156, 76, 74);
        outKnob_.setBounds   ((w - 76) / 2, 243, 76, 74);
        biasKnob_.setBounds  (10,           326, 36, 36);

        const int btnW = (w - 18) / 2;
        loZBtn_.setBounds   (6,            364, btnW, 24);
        phaseBtn_.setBounds (12 + btnW,    364, btnW, 24);
        autoBtn_.setBounds  (6,            391, btnW, 24);
        hissBtn_.setBounds  (12 + btnW,    391, btnW, 24);
    }

private:
    void setupContinuousKnob (juce::Slider& s, double lo, double hi, double step, std::function<void (float)> cb)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRange (lo, hi, step);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 15);
        s.getProperties().set ("marconiRed", false);
        s.onValueChange = [cb, &s] { cb (static_cast<float> (s.getValue())); };
        addAndMakeVisible (s);
    }

    void setupButton (juce::ToggleButton& b, std::atomic<bool>& target)
    {
        b.onClick = [this, &b, &target]
        {
            target.store (b.getToggleState());
            mod_.syncDspParams (false);
        };
        addAndMakeVisible (b);
    }

    void syncControlsFromState()
    {
        gainKnob_.setValue  (mod_.getState().gainStepIndex.load(), juce::dontSendNotification);
        faderKnob_.setValue (mod_.getState().faderDriveDb.load(),  juce::dontSendNotification);
        outKnob_.setValue   (mod_.getState().outputTrimDb.load(),  juce::dontSendNotification);
        biasKnob_.setValue  (mod_.getState().biasTrimPct.load(),   juce::dontSendNotification);
        loZBtn_.setToggleState   (mod_.getState().loZ.load(),         juce::dontSendNotification);
        phaseBtn_.setToggleState (mod_.getState().phaseInvert.load(), juce::dontSendNotification);
        autoBtn_.setToggleState  (mod_.getState().autoGain.load(),    juce::dontSendNotification);
        hissBtn_.setToggleState  (mod_.getState().analogNoise.load(), juce::dontSendNotification);
        updateGainLabel();
    }

    void updateGainLabel()
    {
        const auto& step = dsp::GainSwitchPL20033::getStep (mod_.getState().gainStepIndex.load());
        threeStage_ = step.threeStageTopology;
        gainReadout_ = step.isMicMode
            ? (juce::String (step.label.data(), step.label.size()) + " dB " + (threeStage_ ? "[3-STG]" : "[2-STG]"))
            : juce::String ("LINE [0 dB]");
        repaint();
    }

    void timerCallback() override
    {
        const auto& d = mod_.getDSP();
        outLevel_ = d.getOutputMeterLinear();
        satLevel_ = d.getSaturationMeter();
        threeStage_ = d.isThreeStageActive();
        repaint (0, 315, getWidth(), 45);
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

    void drawMiniLedLadder (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const float db = (outLevel_ > 1e-5f) ? (20.0f * std::log10 (outLevel_)) : -60.0f;
        const float thresholds[4] = { -30.0f, -18.0f, -6.0f, 0.0f };
        const juce::Colour cols[4] = {
            juce::Colour (0xff2ecc71),
            juce::Colour (0xff2ecc71),
            juce::Colour (0xfff1c40f),
            juce::Colour (0xffe74c3c)
        };

        const float stepX = area.getWidth() / 5.0f;
        for (int i = 0; i < 4; ++i)
        {
            const bool lit = db >= thresholds[i];
            const auto c = juce::Point<float> (area.getX() + stepX * (i + 0.5f), area.getCentreY());
            g.setColour (lit ? cols[i] : cols[i].withBrightness (0.18f));
            g.fillEllipse (c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f);
        }

        // 5th LED: Warm Amber Iron/2N3055 Harmonic Glow
        const auto cIron = juce::Point<float> (area.getX() + stepX * 4.5f, area.getCentreY());
        g.setColour (juce::Colour (0xff3a2a18).interpolatedWith (juce::Colour (0xffff8c24), std::sqrt (satLevel_)));
        g.fillEllipse (cIron.x - 4.5f, cIron.y - 4.5f, 9.0f, 9.0f);
    }

    NV73Module500& mod_;
    ui::NeveLookAndFeel lnf_;

    juce::Slider gainKnob_, faderKnob_, outKnob_, biasKnob_;
    juce::ToggleButton loZBtn_   { "LO-Z" };
    juce::ToggleButton phaseBtn_ { "\xc3\x98" };
    juce::ToggleButton autoBtn_  { "AUTO" };
    juce::ToggleButton hissBtn_  { "HISS" };

    juce::String gainReadout_ { "30 dB [2-STG]" };
    bool  threeStage_ { false };
    float outLevel_   { 0.0f };
    float satLevel_   { 0.0f };
};

inline std::unique_ptr<juce::Component> NV73Module500::createSlotEditor()
{
    return std::make_unique<NV73VerticalStripUI> (*this);
}

} // namespace nv73
