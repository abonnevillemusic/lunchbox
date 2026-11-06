#include "../Source/DSP/Neve1073PreampDSP.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

namespace
{

struct HarmonicAnalysis
{
    double fundamentalMag { 0.0 };
    double h2Mag          { 0.0 };
    double h3Mag          { 0.0 };
    double dcOffset       { 0.0 };
    double h2DbRel        { -140.0 };
    double h3DbRel        { -140.0 };
};

HarmonicAnalysis measureSineResponse (nv73::dsp::Neve1073PreampDSP& dsp,
                                      double sampleRate,
                                      double freqHz,
                                      double amplitudeLinear,
                                      int settleSamples = 12000,
                                      int measureSamples = 16384)
{
    dsp.reset();
    std::vector<float> buf (static_cast<std::size_t> (settleSamples + measureSamples), 0.0f);
    for (std::size_t i = 0; i < buf.size(); ++i)
    {
        const double phase = 2.0 * std::numbers::pi * freqHz * static_cast<double> (i) / sampleRate;
        buf[i] = static_cast<float> (amplitudeLinear * std::sin (phase));
    }

    float* ptr = buf.data();
    dsp.processBlock (&ptr, 1, static_cast<int> (buf.size()));

    // Discrete Fourier projection at f0, 2*f0, 3*f0 with Hann window
    auto goertzelWindowed = [&] (double targetHz) -> double
    {
        double re = 0.0, im = 0.0;
        double winSum = 0.0;
        for (int n = 0; n < measureSamples; ++n)
        {
            const double w = 0.5 * (1.0 - std::cos (2.0 * std::numbers::pi * n / (measureSamples - 1)));
            const double x = static_cast<double> (buf[static_cast<std::size_t> (settleSamples + n)]) * w;
            const double ang = 2.0 * std::numbers::pi * targetHz * n / sampleRate;
            re += x * std::cos (ang);
            im -= x * std::sin (ang);
            winSum += w;
        }
        return (2.0 * std::hypot (re, im)) / winSum;
    };

    double dc = 0.0;
    for (int n = 0; n < measureSamples; ++n)
        dc += buf[static_cast<std::size_t> (settleSamples + n)];
    dc /= static_cast<double> (measureSamples);

    HarmonicAnalysis res;
    res.fundamentalMag = goertzelWindowed (freqHz);
    res.h2Mag          = goertzelWindowed (2.0 * freqHz);
    res.h3Mag          = goertzelWindowed (3.0 * freqHz);
    res.dcOffset       = dc;
    res.h2DbRel        = 20.0 * std::log10 ((res.h2Mag + 1e-18) / (res.fundamentalMag + 1e-18));
    res.h3DbRel        = 20.0 * std::log10 ((res.h3Mag + 1e-18) / (res.fundamentalMag + 1e-18));
    return res;
}

} // namespace

int main()
{
    using namespace nv73::dsp;

    constexpr double kSampleRate = 48000.0;
    Neve1073PreampDSP dsp;
    dsp.prepare (kSampleRate);

    int failures = 0;
    auto check = [&] (bool cond, const char* msg)
    {
        if (! cond)
        {
            std::cerr << "[FAIL] " << msg << "\n";
            ++failures;
        }
        else
        {
            std::cout << "[PASS] " << msg << "\n";
        }
    };

    std::cout << "=== 1. Stepped PL20033 Gain Accuracy (Hardware Mode, Auto-Gain OFF) ===\n";
    for (int idx = 0; idx < GainSwitchPL20033::kNumSteps; ++idx)
    {
        PreampParams p;
        p.gainStepIndex = idx;
        p.autoGainComp  = false;
        p.loZImpedance  = false;
        p.oversampling  = Oversampler::Factor::x4;
        dsp.setParameters (p, true);

        const auto& spec = GainSwitchPL20033::getStep (idx);
        // Choose small input so even +80 dB stays purely in small-signal linear region
        const double inAmp = std::pow (10.0, (-spec.totalNominalGainDb - 28.0) / 20.0);
        const auto m = measureSineResponse (dsp, kSampleRate, 1000.0, inAmp);
        const double measuredGainDb = 20.0 * std::log10 (m.fundamentalMag / inAmp);
        const double expectedGainDb = spec.totalNominalGainDb + (spec.threeStageTopology ? -0.25 : 0.0);

        std::cout << "  Step " << std::setw (2) << idx << " [" << std::setw (4) << spec.label << " dB] "
                  << (spec.threeStageTopology ? "(3-Stage BA284+BA283)" : "(2-Stage BA283)      ")
                  << " -> Measured: " << std::fixed << std::setprecision (2) << measuredGainDb << " dB"
                  << " (Expected ~" << expectedGainDb << " dB)\n";

        if (std::abs (measuredGainDb - expectedGainDb) > 0.65)
        {
            std::cerr << "  Gain error out of tolerance at step " << idx << "\n";
            ++failures;
        }
    }
    check (failures == 0, "All 14 PL20033 gain steps track hardware gain within +/-0.65 dB");

    std::cout << "\n=== 2. 50 dB -> 55 dB Topology Switch & Class-A Even-Harmonic Asymmetry ===\n";
    {
        PreampParams p;
        p.gainStepIndex = 4; // 35 dB warm push with Auto-Gain ON
        p.autoGainComp  = true;
        p.oversampling  = Oversampler::Factor::x4;
        dsp.setParameters (p, true);
        check (! dsp.isThreeStageActive(), "At 35 dB, 2-stage topology is active");

        // Moderate -12 dBFS @ 400 Hz -> Class-A 2nd harmonic should dominate over 3rd harmonic!
        const auto warm = measureSineResponse (dsp, kSampleRate, 400.0, 0.25);
        std::cout << "  400 Hz @ -12 dBFS (35 dB mode): H2 = " << warm.h2DbRel
                  << " dBc, H3 = " << warm.h3DbRel << " dBc, DC = " << warm.dcOffset << "\n";
        check (warm.h2DbRel > warm.h3DbRel + 3.0, "Single-ended Class-A + DC-biased LO1166 produces dominant 2nd harmonic warmth");
        check (std::abs (warm.dcOffset) < 1e-3, "Coupling-cap DC blockers keep output DC offset negligible");

        p.gainStepIndex = 8; // 55 dB -> engages 3-stage BA284 + BA283NV!
        dsp.setParameters (p, true);
        check (dsp.isThreeStageActive(), "Clicking from 50 dB to 55 dB engages 3-stage BA284 + BA283NV cascade");
    }

    std::cout << "\n=== 3. Frequency-Dependent Marinair Transformer Core Flux Saturation ===\n";
    {
        PreampParams p;
        p.gainStepIndex = 2; // 25 dB
        p.autoGainComp  = true;
        p.oversampling  = Oversampler::Factor::x4;
        dsp.setParameters (p, true);

        const auto lowBass = measureSineResponse (dsp, kSampleRate, 55.0,  0.35);
        const auto midTone = measureSineResponse (dsp, kSampleRate, 1000.0, 0.35);
        std::cout << "  55 Hz   Transformer H2=" << lowBass.h2DbRel << " dBc, H3=" << lowBass.h3DbRel << " dBc\n";
        std::cout << "  1000 Hz Transformer H2=" << midTone.h2DbRel << " dBc, H3=" << midTone.h3DbRel << " dBc\n";
        check (lowBass.h2Mag > midTone.h2Mag * 1.5 || lowBass.h3Mag > midTone.h3Mag * 1.5,
               "Low-frequency magnetic flux (Phi ~ V/f) saturates transformer core richer than 1 kHz");
    }

    std::cout << "\n=== 4. Lo-Z (300 ohm) Switch, Phase Invert (Ø), & Extreme Overload Stability ===\n";
    {
        PreampParams p;
        p.gainStepIndex = 3; // 30 dB
        p.autoGainComp  = false;
        p.loZImpedance  = false;
        dsp.setParameters (p, true);
        const auto hiZ = measureSineResponse (dsp, kSampleRate, 1000.0, 0.002);

        p.loZImpedance = true;
        dsp.setParameters (p, true);
        const auto loZ = measureSineResponse (dsp, kSampleRate, 1000.0, 0.002);
        const double stepUpDiffDb = 20.0 * std::log10 (loZ.fundamentalMag / hiZ.fundamentalMag);
        std::cout << "  Lo-Z (300 ohm) vs Hi-Z (1200 ohm) step-up delta: +" << stepUpDiffDb << " dB\n";
        check (stepUpDiffDb > 2.0 && stepUpDiffDb < 3.0, "Lo-Z 300-ohm primary wiring increases transformer step-up by ~2.5 dB");

        // Phase invert check
        p.phaseInvert = false;
        dsp.setParameters (p, true);
        dsp.reset();
        float sNormal = 0.005f;
        float* pNorm = &sNormal;
        for (int i = 0; i < 64; ++i) { sNormal = 0.005f; dsp.processBlock (&pNorm, 1, 1); }

        p.phaseInvert = true;
        dsp.setParameters (p, true);
        dsp.reset();
        float sInv = 0.005f;
        float* pInv = &sInv;
        for (int i = 0; i < 64; ++i) { sInv = 0.005f; dsp.processBlock (&pInv, 1, 1); }
        check (std::abs (sNormal + sInv) < 1e-6f, "Phase Invert (Ø) produces exact 180-degree polarity inversion");

        // Torture test: +80 dB gain with +/-100.0 (+40 dBFS) extreme transient spikes
        p.gainStepIndex = 13; // 80 dB
        p.autoGainComp  = false;
        dsp.setParameters (p, true);
        std::vector<float> torture (4096, 0.0f);
        for (std::size_t i = 0; i < torture.size(); ++i)
            torture[i] = (i % 37 == 0) ? 100.0f : ((i % 53 == 0) ? -100.0f : 0.5f);
        float* pTorture = torture.data();
        dsp.processBlock (&pTorture, 1, static_cast<int> (torture.size()));
        bool allFinite = true;
        for (float v : torture)
            if (! std::isfinite (v))
                allFinite = false;
        check (allFinite, "Unconditionally stable with zero NaNs/Infs under +40 dBFS torture transients at +80 dB gain");
    }

    if (failures > 0)
    {
        std::cerr << "\nFAILED with " << failures << " error(s).\n";
        return EXIT_FAILURE;
    }

    std::cout << "\nALL NEVE 1073 DSP CIRCUIT VERIFICATION CHECKS PASSED!\n";
    return EXIT_SUCCESS;
}
