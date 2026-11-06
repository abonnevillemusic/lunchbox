#include "../Source/DSP/Neve1073EQDSP.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;

double measureSteadySineGainDb (nv73eq::dsp::Neve1073EQDSP& dsp,
                                const nv73eq::dsp::EQParams& params,
                                double freqHz,
                                float amplitude = 0.025f,
                                int totalSamples = 16384)
{
    dsp.reset();
    dsp.setParameters (params, 1.0, true);

    std::vector<float> bufL (static_cast<std::size_t> (totalSamples));
    std::vector<float> bufR (static_cast<std::size_t> (totalSamples));
    for (int n = 0; n < totalSamples; ++n)
    {
        const float s = amplitude * static_cast<float> (std::sin (2.0 * std::numbers::pi * freqHz * n / kSampleRate));
        bufL[static_cast<std::size_t> (n)] = s;
        bufR[static_cast<std::size_t> (n)] = s;
    }

    float* ptrs[2] = { bufL.data(), bufR.data() };
    dsp.processBlock (ptrs, 2, totalSamples);

    double peakOut = 0.0;
    for (int n = totalSamples / 2; n < totalSamples; ++n)
        peakOut = std::max (peakOut, static_cast<double> (std::abs (bufL[static_cast<std::size_t> (n)])));

    return 20.0 * std::log10 (std::max (1e-12, peakOut / static_cast<double> (amplitude)));
}

} // namespace

int main()
{
    using namespace nv73eq::dsp;

    Neve1073EQDSP dsp;
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

    std::cout << std::fixed << std::setprecision (2);

    // -------------------------------------------------------------------------
    // 1. Flat Unity Response & EQL Hard-Wire Bypass Check
    // -------------------------------------------------------------------------
    std::cout << "=== 1. Flat Unity Response & EQL Hard-Wire Switch ===\n";
    {
        EQParams flat;
        const double g1k = measureSteadySineGainDb (dsp, flat, 1000.0);
        std::cout << "  Flat EQL=ON @ 1 kHz: " << g1k << " dB\n";
        check (std::abs (g1k) < 0.25, "Flat 1073LBEQ passes 1 kHz within +/-0.25 dB of unity");

        EQParams boosted = flat;
        boosted.hfGainDb  = 14.0;
        boosted.midGainDb = 15.0;
        boosted.lowGainDb = 14.0;
        boosted.eqEnabled = false; // Disengage EQL switch!
        const double gBypass = measureSteadySineGainDb (dsp, boosted, 1600.0);
        std::cout << "  Boosted bands with EQL=OFF @ 1.6 kHz: " << gBypass << " dB\n";
        check (std::abs (gBypass) < 0.15, "Disengaging EQL hard-bypasses B205 + B211 + BA284 cleanly");
    }

    // -------------------------------------------------------------------------
    // 2. B205 Active Baxandall 12 kHz High Shelf & Switched Low Shelf
    // -------------------------------------------------------------------------
    std::cout << "\n=== 2. B205 Active Baxandall 12 kHz HF & Switched LF Shelves ===\n";
    {
        EQParams hfBoost;
        hfBoost.hfGainDb = 12.0;
        const double g18kBoost = measureSteadySineGainDb (dsp, hfBoost, 18000.0);
        const double g200Boost = measureSteadySineGainDb (dsp, hfBoost, 200.0);

        EQParams hfCut;
        hfCut.hfGainDb = -12.0;
        const double g18kCut = measureSteadySineGainDb (dsp, hfCut, 18000.0);

        std::cout << "  HF +12 dB -> @18 kHz: " << g18kBoost << " dB, @200 Hz: " << g200Boost
                  << " dB | HF -12 dB -> @18 kHz: " << g18kCut << " dB\n";
        check (g18kBoost > 8.5 && g18kBoost < 13.0, "12 kHz High Shelf +12 dB boosts upper air band accurately");
        check (std::abs (g200Boost) < 0.30, "12 kHz High Shelf leaves 200 Hz fundamental untouched");
        check (g18kCut < -8.5 && g18kCut > -13.0, "12 kHz High Shelf -12 dB cuts upper air band symmetrically");

        EQParams lfBoost;
        lfBoost.lowFreqStep = 3; // 110 Hz
        lfBoost.lowGainDb   = 14.0;
        const double g25Boost = measureSteadySineGainDb (dsp, lfBoost, 25.0);
        const double g2kBoost = measureSteadySineGainDb (dsp, lfBoost, 2000.0);

        EQParams lfCut = lfBoost;
        lfCut.lowGainDb = -14.0;
        const double g25Cut = measureSteadySineGainDb (dsp, lfCut, 25.0);

        std::cout << "  LF 110 Hz +14 dB -> @25 Hz: " << g25Boost << " dB, @2 kHz: " << g2kBoost
                  << " dB | LF -14 dB -> @25 Hz: " << g25Cut << " dB\n";
        check (g25Boost > 12.2 && g25Boost < 14.8, "110 Hz Low Shelf +14 dB delivers full sub-bass weight");
        check (std::abs (g2kBoost) < 0.25, "110 Hz Low Shelf leaves 2 kHz midrange clean");
        check (g25Cut < -12.2 && g25Cut > -14.8, "110 Hz Low Shelf -14 dB attenuates sub-bass symmetrically");
    }

    // -------------------------------------------------------------------------
    // 3. B211 T1295/T1280 6-Tap Inductor Mid Bell, Proportional-Q & Core Saturation
    // -------------------------------------------------------------------------
    std::cout << "\n=== 3. B211 6-Tap Inductor Mid Bell, Proportional-Q & Core Saturation ===\n";
    {
        const double tapFreqs[6] = { 360.0, 700.0, 1600.0, 3200.0, 4800.0, 7200.0 };
        bool allTapsAccurate = true;
        for (int step = 1; step <= 6; ++step)
        {
            EQParams mp;
            mp.midFreqStep = step;
            mp.midGainDb   = 12.0;
            const double centerDb = measureSteadySineGainDb (dsp, mp, tapFreqs[step - 1], 0.015f);
            std::cout << "  Tap " << step << " (" << tapFreqs[step - 1] << " Hz) @ +12 dB -> measured "
                      << centerDb << " dB\n";
            if (std::abs (centerDb - 12.0) > 0.65)
                allTapsAccurate = false;
        }
        check (allTapsAccurate, "All 6 B211 inductor taps (360 Hz .. 7.2 kHz) hit +12 dB center target within +/-0.65 dB");

        // Verify Proportional-Q tightening (+4 dB broad vs +18 dB focused)
        const double qLow  = B211InductorMidBell::computeEffectiveQ (3, 4.0);
        const double qHigh = B211InductorMidBell::computeEffectiveQ (3, 18.0);
        std::cout << "  1.6 kHz Proportional-Q: Q(@+4 dB) = " << qLow << ", Q(@+18 dB) = " << qHigh << "\n";
        check (qHigh > qLow * 1.8, "B211 inductor network exhibits authentic proportional-Q tightening at high boost");

        // Verify Nonlinear T1295 Core Saturation compresses hot resonant hits gracefully!
        EQParams hotMid;
        hotMid.midFreqStep = 1; // 360 Hz (T1295 iron-core tap)
        hotMid.midGainDb   = 16.0;
        const double smallSigGainDb = measureSteadySineGainDb (dsp, hotMid, 360.0, 0.01f);
        const double hotSigGainDb   = measureSteadySineGainDb (dsp, hotMid, 360.0, 0.35f);
        std::cout << "  T1295 360 Hz +16 dB Core Compression: Small-signal = " << smallSigGainDb
                  << " dB vs Hot-drive = " << hotSigGainDb << " dB\n";
        check (smallSigGainDb - hotSigGainDb > 1.2,
               "T1295 iron-core inductor + BA284 Class-A stage softly compress hot resonant peaks (>1.2 dB dynamic taming)");
    }

    // -------------------------------------------------------------------------
    // 4. BA182C Passive 18 dB/Octave Inductor HPF & Resonant Passband Bump
    // -------------------------------------------------------------------------
    std::cout << "\n=== 4. BA182C Passive 18 dB/Octave Inductor HPF (50 / 80 / 160 / 300 Hz) ===\n";
    {
        EQParams hpf80;
        hpf80.eqEnabled = false; // Verify HPF remains active even when EQL is OFF (true 1073 hardware wiring!)
        hpf80.hpfStep   = 2;     // 80 Hz

        const double g40  = measureSteadySineGainDb (dsp, hpf80, 40.0,  0.05f); // 1 octave below 80 Hz
        const double g80  = measureSteadySineGainDb (dsp, hpf80, 80.0,  0.05f); // Cutoff fc
        const double g118 = measureSteadySineGainDb (dsp, hpf80, 118.0, 0.05f); // ~1.45 * fc resonant bump!

        std::cout << "  BA182C 80 Hz HPF (with EQL=OFF): @40 Hz = " << g40
                  << " dB, @80 Hz = " << g80 << " dB, @118 Hz (bump) = +" << g118 << " dB\n";

        check (std::abs (g80 - (-3.0)) < 0.85, "BA182C 80 Hz HPF hits -3 dB cutoff point accurately (even with EQL=OFF)");
        check (g40 < -17.5 && g40 > -22.5, "BA182C 80 Hz HPF rolls off at steep 3rd-order ~18-20 dB/octave below cutoff");
        check (g118 > 0.40 && g118 < 1.15, "BA182C 5.1k-terminated inductor exhibits authentic +0.7 dB passband punch bump");
    }

    // -------------------------------------------------------------------------
    // 5. Extreme Boost Overload Stability Check (No NaN / Inf / Runaway)
    // -------------------------------------------------------------------------
    std::cout << "\n=== 5. Extreme Multi-Band Boost Overload Stability ===\n";
    {
        EQParams extreme;
        extreme.hfGainDb    = 16.0;
        extreme.midFreqStep = 4; // 3.2 kHz
        extreme.midGainDb   = 18.0;
        extreme.lowFreqStep = 2; // 60 Hz
        extreme.lowGainDb   = 16.0;
        extreme.hpfStep     = 1; // 50 Hz

        dsp.reset();
        dsp.setParameters (extreme, 0.92, true); // Also test with sagged +/-14.7V Lunchbox rail!

        std::vector<float> l (4096, 0.95f), r (4096, -0.95f);
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            l[i] = 0.95f * std::sin (0.13f * static_cast<float> (i));
            r[i] = 0.95f * std::cos (0.07f * static_cast<float> (i));
        }
        float* ptrs[2] = { l.data(), r.data() };
        dsp.processBlock (ptrs, 2, 4096);

        bool finiteAndBounded = true;
        for (float s : l)
            if (! std::isfinite (s) || std::abs (s) > 12.0f)
                finiteAndBounded = false;

        check (finiteAndBounded, "All-bands-maxed (+16/+18/+16 dB) hot signal remains 100% finite and Class-A bounded");
    }

    if (failures > 0)
    {
        std::cerr << "\nFAILED with " << failures << " error(s).\n";
        return EXIT_FAILURE;
    }

    std::cout << "\nALL NV-73EQ (1073LBEQ) CIRCUIT & DSP VERIFICATION TESTS PASSED!\n";
    return EXIT_SUCCESS;
}
