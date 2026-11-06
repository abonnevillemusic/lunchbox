#include "PluginEditor.h"
#include <cmath>

Neve1073EQAudioProcessorEditor::Neve1073EQAudioProcessorEditor (Neve1073EQAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef_ (p)
{
    setLookAndFeel (&lnf_);

    auto setupKnob = [this] (juce::Slider& s, bool isRed)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 18);
        s.getProperties().set ("marconiRed", isRed);
        s.onValueChange = [this] { repaint(); };
        addAndMakeVisible (s);
    };

    setupKnob (lowKnob_,  false);
    setupKnob (midKnob_,  false);
    setupKnob (hfKnob_,   false);
    setupKnob (trimKnob_, true);

    auto setupCombo = [this] (juce::ComboBox& box, const juce::StringArray& items)
    {
        for (int i = 0; i < items.size(); ++i)
            box.addItem (items[i], i + 1);
        box.onChange = [this] { repaint(); };
        addAndMakeVisible (box);
    };

    setupCombo (hpfBox_,        { "OFF", "50 Hz", "80 Hz", "160 Hz", "300 Hz" });
    setupCombo (lowFreqBox_,    { "OFF", "35 Hz", "60 Hz", "110 Hz", "220 Hz" });
    setupCombo (midFreqBox_,    { "OFF", "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" });
    setupCombo (oversampleBox_, { "1x (Off)", "2x", "4x (Cauer)" });

    eqInBtn_.onClick  = [this] { repaint(); };
    phaseBtn_.onClick = [this] { repaint(); };
    addAndMakeVisible (eqInBtn_);
    addAndMakeVisible (phaseBtn_);

    auto& apvts = processorRef_.getAPVTS();
    hfAttach_      = std::make_unique<SliderAttachment> (apvts, "hfGain",       hfKnob_);
    midFreqAttach_ = std::make_unique<ComboAttachment>  (apvts, "midFreq",      midFreqBox_);
    midGainAttach_ = std::make_unique<SliderAttachment> (apvts, "midGain",      midKnob_);
    lowFreqAttach_ = std::make_unique<ComboAttachment>  (apvts, "lowFreq",      lowFreqBox_);
    lowGainAttach_ = std::make_unique<SliderAttachment> (apvts, "lowGain",      lowKnob_);
    hpfAttach_     = std::make_unique<ComboAttachment>  (apvts, "hpfFreq",      hpfBox_);
    trimAttach_    = std::make_unique<SliderAttachment> (apvts, "outputTrim",   trimKnob_);
    eqInAttach_    = std::make_unique<ButtonAttachment> (apvts, "eqIn",         eqInBtn_);
    phaseAttach_   = std::make_unique<ButtonAttachment> (apvts, "phaseInvert",  phaseBtn_);
    osAttach_      = std::make_unique<ComboAttachment>  (apvts, "oversampling", oversampleBox_);

    setSize (780, 480);
    startTimerHz (30);
}

Neve1073EQAudioProcessorEditor::~Neve1073EQAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Neve1073EQAudioProcessorEditor::timerCallback()
{
    const auto& d = processorRef_.getDSP();
    inLinear_  = d.getInputMeter();
    outLinear_ = d.getOutputMeter();
    glowNorm_  = d.getInductorGlow();
    repaint (18, getHeight() - 62, getWidth() - 36, 52);
}

void Neve1073EQAudioProcessorEditor::paint (juce::Graphics& g)
{
    using nv73::ui::NeveLookAndFeel;
    const auto b = getLocalBounds().toFloat();

    // RAF Blue-Grey Console Chassis
    juce::ColourGradient bgGrad (NeveLookAndFeel::kFaceplateBase.brighter (0.06f), 0.0f, 0.0f,
                                 NeveLookAndFeel::kFaceplateDark, 0.0f, b.getHeight(), false);
    g.setGradientFill (bgGrad);
    g.fillAll();

    // Brushed aluminum left & right console trim rails
    g.setColour (NeveLookAndFeel::kRailSilver.darker (0.25f));
    g.fillRect (0.0f, 0.0f, 10.0f, b.getHeight());
    g.fillRect (b.getWidth() - 10.0f, 0.0f, 10.0f, b.getHeight());

    // Header Title Bar
    g.setColour (NeveLookAndFeel::kCreamLegend);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("NV-73EQ   3-BAND TAPPED-INDUCTOR EQUALIZER", 22, 10, 480, 24, juce::Justification::centredLeft);

    g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.72f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::plain));
    g.drawText ("B205 BAXANDALL  \xe2\x80\xa2  B211 T1295/T1280 INDUCTORS  \xe2\x80\xa2  BA182C HPF  \xe2\x80\xa2  BA284 CLASS-A",
                22, 31, 500, 16, juce::Justification::centredLeft);

    g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.85f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("OVERSAMPLING:", getWidth() - 238, 15, 105, 22, juce::Justification::centredRight);

    // 1. Real-time Composite EQ + HPF Frequency Response Display
    drawResponseDisplay (g, juce::Rectangle<float> (22.0f, 54.0f, b.getWidth() - 44.0f, 156.0f));

    // 2. 4 Recessed Console EQ Control Bays (y = 220 .. 408)
    const float bayY = 220.0f;
    const float bayH = 186.0f;
    const float bayW = (b.getWidth() - 44.0f - 30.0f) / 4.0f; // 4 bays with 10px gaps

    const char* bayTitles[4] = {
        "BA182C 18dB/OCT HPF",
        "B205 LOW SHELF",
        "B211 INDUCTOR MID BELL",
        "12 kHz HF & OUTPUT TRIM"
    };

    for (int i = 0; i < 4; ++i)
    {
        const float bx = 22.0f + i * (bayW + 10.0f);
        const juce::Rectangle<float> bay (bx, bayY, bayW, bayH);
        g.setColour (juce::Colour (0xff262f38));
        g.fillRoundedRectangle (bay, 6.0f);
        g.setColour (juce::Colour (0xff4a5866));
        g.drawRoundedRectangle (bay.reduced (0.5f), 6.0f, 1.1f);

        g.setColour (NeveLookAndFeel::kCreamLegend);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (bayTitles[i], bay.withHeight (24.0f).toNearestInt(), juce::Justification::centred);
    }

    // Sub-labels in Bay 1 & Bay 4
    g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.80f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    const float b0x = 22.0f;
    g.drawText ("CUTOFF (5.1k TERM)", static_cast<int> (b0x + 12), 250, static_cast<int> (bayW - 24), 14, juce::Justification::centred);

    const float b3x = 22.0f + 3.0f * (bayW + 10.0f);
    g.drawText ("HF 12 kHz", static_cast<int> (b3x + 6),             246, static_cast<int> (bayW * 0.5f - 8), 14, juce::Justification::centred);
    g.drawText ("OUT TRIM",  static_cast<int> (b3x + bayW * 0.5f),   246, static_cast<int> (bayW * 0.5f - 6), 14, juce::Justification::centred);

    // 3. Bottom Meter & T1295/BA284 Inductor Saturation Strip
    drawMeterStrip (g, juce::Rectangle<float> (22.0f, 416.0f, b.getWidth() - 44.0f, 52.0f));
}

void Neve1073EQAudioProcessorEditor::drawResponseDisplay (juce::Graphics& g, juce::Rectangle<float> area) const
{
    using nv73::ui::NeveLookAndFeel;

    g.setColour (juce::Colour (0xff12171c));
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (juce::Colour (0xff4a5866));
    g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.2f);

    const auto plot = area.reduced (34.0f, 12.0f);

    auto freqToX = [&plot] (double f) -> float
    {
        const double norm = std::log10 (f / 20.0) / std::log10 (22000.0 / 20.0);
        return plot.getX() + static_cast<float> (norm) * plot.getWidth();
    };
    auto dbToY = [&plot] (double db) -> float
    {
        const double clamped = std::clamp (db, -20.0, 20.0);
        return plot.getCentreY() - static_cast<float> (clamped / 20.0) * (plot.getHeight() * 0.5f);
    };

    // dB Horizontal Grid (-18, -12, -6, 0, +6, +12, +18 dB)
    g.setFont (juce::FontOptions (9.5f, juce::Font::plain));
    for (int db : { -18, -12, -6, 0, 6, 12, 18 })
    {
        const float y = dbToY (static_cast<double> (db));
        g.setColour (db == 0 ? juce::Colours::white.withAlpha (0.26f) : juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (static_cast<int> (std::round (y)), plot.getX(), plot.getRight());

        g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.60f));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                    static_cast<int> (area.getX() + 3), static_cast<int> (y - 6), 28, 12,
                    juce::Justification::centredRight);
    }

    // Authentic 1073LBEQ Stepped Frequency Grid Lines
    const std::pair<double, const char*> freqTicks[] = {
        { 35.0,    "35" },
        { 60.0,    "60" },
        { 110.0,   "110" },
        { 220.0,   "220" },
        { 360.0,   "360" },
        { 700.0,   "700" },
        { 1600.0,  "1.6k" },
        { 3200.0,  "3.2k" },
        { 4800.0,  "4.8k" },
        { 7200.0,  "7.2k" },
        { 12000.0, "12k" }
    };
    for (const auto& [f, lbl] : freqTicks)
    {
        const float x = freqToX (f);
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.drawVerticalLine (static_cast<int> (std::round (x)), plot.getY(), plot.getBottom());
        g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.58f));
        g.drawText (lbl, static_cast<int> (x - 16), static_cast<int> (plot.getBottom() + 1), 32, 11,
                    juce::Justification::centred);
    }

    // Evaluate composite curve
    const auto p = processorRef_.readCurrentParams();
    juce::Path curvePath;
    juce::Path fillPath;
    const int numPts = 220;
    const float zeroY = dbToY (0.0);

    for (int i = 0; i <= numPts; ++i)
    {
        const double norm = static_cast<double> (i) / static_cast<double> (numPts);
        const double freq = 20.0 * std::pow (22000.0 / 20.0, norm);
        const double db   = nv73eq::dsp::Neve1073EQDSP::evaluateCompositeMagDb (freq, p);
        const float px    = plot.getX() + static_cast<float> (norm) * plot.getWidth();
        const float py    = dbToY (db);

        if (i == 0)
        {
            curvePath.startNewSubPath (px, py);
            fillPath.startNewSubPath  (px, zeroY);
            fillPath.lineTo           (px, py);
        }
        else
        {
            curvePath.lineTo (px, py);
            fillPath.lineTo  (px, py);
        }
    }
    fillPath.lineTo (plot.getRight(), zeroY);
    fillPath.closeSubPath();

    const juce::Colour strokeCol = p.eqEnabled ? juce::Colour (0xfff5a623) : juce::Colour (0xff60a5fa);
    g.setColour (strokeCol.withAlpha (0.16f));
    g.fillPath (fillPath);

    g.setColour (strokeCol);
    g.strokePath (curvePath, juce::PathStrokeType (2.3f));
}

void Neve1073EQAudioProcessorEditor::drawMeterStrip (juce::Graphics& g, juce::Rectangle<float> area) const
{
    using nv73::ui::NeveLookAndFeel;

    g.setColour (juce::Colour (0xff1b2127));
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (juce::Colour (0xff4a5866));
    g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.1f);

    auto inner = area.reduced (12.0f, 7.0f);
    auto rightLamp = inner.removeFromRight (210.0f);

    // Warm Amber T1295 / BA284 Harmonic Inductor Glow Lamp
    const juce::Point<float> lc (rightLamp.getRight() - 12.0f, rightLamp.getCentreY());
    if (glowNorm_ > 0.02f)
    {
        g.setColour (juce::Colour (0xfff59e0b).withAlpha (0.35f * glowNorm_));
        g.fillEllipse (lc.x - 13.0f, lc.y - 13.0f, 26.0f, 26.0f);
    }
    g.setColour (juce::Colour (0xff3a2e22).interpolatedWith (juce::Colour (0xffff8524), std::sqrt (glowNorm_)));
    g.fillEllipse (lc.x - 6.5f, lc.y - 6.5f, 13.0f, 13.0f);
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.drawEllipse (lc.x - 6.5f, lc.y - 6.5f, 13.0f, 13.0f, 1.0f);

    g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.88f));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("T1295 / BA284 DRIVE", rightLamp.withTrimmedRight (26.0f).toNearestInt(), juce::Justification::centredRight);

    inner.removeFromRight (12.0f);

    auto drawBar = [&] (juce::Rectangle<float> row, const char* lbl, float lin)
    {
        const float db = (lin > 1e-5f) ? std::clamp (20.0f * std::log10 (lin), -48.0f, 6.0f) : -48.0f;
        const float norm = std::clamp ((db + 48.0f) / 54.0f, 0.0f, 1.0f);

        auto lRect = row.removeFromLeft (34.0f);
        g.setColour (NeveLookAndFeel::kCreamLegend.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (lbl, lRect.toNearestInt(), juce::Justification::centredLeft);

        auto track = row.reduced (0.0f, 2.0f);
        g.setColour (juce::Colour (0xff0f1317));
        g.fillRoundedRectangle (track, 3.0f);
        if (norm > 0.005f)
        {
            juce::ColourGradient mg (juce::Colour (0xff38b26f), track.getX(), track.getY(),
                                     juce::Colour (0xffeab308), track.getX() + track.getWidth() * 0.78f, track.getY(), false);
            mg.addColour (0.94, juce::Colour (0xffef4444));
            g.setGradientFill (mg);
            g.fillRoundedRectangle (track.withWidth (track.getWidth() * norm), 3.0f);
        }
    };

    auto inRow = inner.removeFromTop (16.0f);
    inner.removeFromTop (5.0f);
    auto outRow = inner.removeFromTop (16.0f);
    drawBar (inRow,  "IN",  inLinear_);
    drawBar (outRow, "OUT", outLinear_);
}

void Neve1073EQAudioProcessorEditor::resized()
{
    oversampleBox_.setBounds (getWidth() - 128, 14, 106, 24);

    const float bayY = 220.0f;
    const float bayW = (static_cast<float> (getWidth()) - 44.0f - 30.0f) / 4.0f;

    // Bay 0: BA182C HPF + EQL + Phase
    {
        const int bx = static_cast<int> (22.0f);
        const int bw = static_cast<int> (bayW);
        hpfBox_.setBounds   (bx + 14, static_cast<int> (bayY) + 48, bw - 28, 26);
        eqInBtn_.setBounds  (bx + 14, static_cast<int> (bayY) + 92, bw - 28, 34);
        phaseBtn_.setBounds (bx + 14, static_cast<int> (bayY) + 134, bw - 28, 32);
    }

    // Bay 1: B205 Low Shelf
    {
        const int bx = static_cast<int> (22.0f + 1.0f * (bayW + 10.0f));
        const int bw = static_cast<int> (bayW);
        lowKnob_.setBounds    (bx + (bw - 104) / 2, static_cast<int> (bayY) + 28, 104, 114);
        lowFreqBox_.setBounds (bx + 18,             static_cast<int> (bayY) + 148, bw - 36, 25);
    }

    // Bay 2: B211 Inductor Mid Bell
    {
        const int bx = static_cast<int> (22.0f + 2.0f * (bayW + 10.0f));
        const int bw = static_cast<int> (bayW);
        midKnob_.setBounds    (bx + (bw - 104) / 2, static_cast<int> (bayY) + 28, 104, 114);
        midFreqBox_.setBounds (bx + 18,             static_cast<int> (bayY) + 148, bw - 36, 25);
    }

    // Bay 3: HF 12 kHz Shelf + Red Marconi Output Trim
    {
        const int bx = static_cast<int> (22.0f + 3.0f * (bayW + 10.0f));
        const int bw = static_cast<int> (bayW);
        const int halfW = bw / 2;
        hfKnob_.setBounds   (bx + 4,         static_cast<int> (bayY) + 44, halfW - 6, 122);
        trimKnob_.setBounds (bx + halfW + 2, static_cast<int> (bayY) + 44, halfW - 6, 122);
    }
}
