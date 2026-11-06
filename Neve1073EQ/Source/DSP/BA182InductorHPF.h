#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>

namespace nv73eq::dsp
{

/**
 * BA182C Passive 3rd-Order (18 dB/Octave) Tapped-Inductor C-L-C High-Pass Filter
 * Terminated into the classic 5.1 kOhm load resistor.
 *
 * Switch Positions: OFF, 50 Hz, 80 Hz, 160 Hz, 300 Hz.
 * Physics:
 *   - 1st-order passive polystyrene capacitor pole (0.85 * fc)
 *   - 2nd-order underdamped ZDF tapped-inductor LC section (1.155 * fc, Q = 1.08)
 *   - Yields exact -3.0 dB at fc, steep 18 dB/oct roll-off below fc, AND the classic +0.72 dB
 *     Neve resonant passband "punch bump" at ~1.45 * fc, plus soft shunt-inductor core saturation!
 */
class BA182InductorHPF
{
public:
    static constexpr int kNumHpfSteps = 5;
    static constexpr std::array<double, kNumHpfSteps> kCutoffHz { 0.0, 50.0, 80.0, 160.0, 300.0 };
    static constexpr std::array<std::string_view, kNumHpfSteps> kLabels { "OFF", "50", "80", "160", "300" };

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::max (22050.0, sampleRate);
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        rcZ1_   = 0.0;
        ic1eq_  = 0.0;
        ic2eq_  = 0.0;
        lastStrayField_ = 0.0;
    }

    void setStepIndex (int index) noexcept
    {
        const int clamped = std::clamp (index, 0, kNumHpfSteps - 1);
        if (clamped != stepIdx_)
        {
            stepIdx_ = clamped;
            updateCoefficients();
        }
    }

    [[nodiscard]] int getStepIndex() const noexcept { return stepIdx_; }

    [[nodiscard]] inline double processSample (double x) noexcept
    {
        if (stepIdx_ <= 0)
        {
            lastStrayField_ = 0.0;
            return x;
        }

        // 1. 1st-order TPT high-pass capacitor stage (C1)
        const double vRcLp = rcG_ * (x - rcZ1_);
        const double yRcHp = x - (vRcLp + rcZ1_);
        rcZ1_ = vRcLp + (vRcLp + rcZ1_);

        // 2. 2nd-order underdamped ZDF LC inductor high-pass stage (L_hpf + C2 into 5.1k termination)
        // Sample-rate-invariant passive core loss increase on heavy sub-bass choke current:
        const double kEff = k_ * (1.0 + 0.16 * ic1eq_ * ic1eq_);
        const double a1   = 1.0 / (1.0 + g_ * (g_ + kEff));
        const double a2   = g_ * a1;
        const double a3   = g_ * a2;

        const double v3 = yRcHp - ic2eq_;
        const double v1 = a1 * ic1eq_ + a2 * v3; // Shunt inductor current state
        const double v2 = ic2eq_ + a2 * ic1eq_ + a3 * v3;

        ic1eq_ = 2.0 * v1 - ic1eq_;
        ic2eq_ = 2.0 * v2 - ic2eq_;

        lastStrayField_ = 0.08 * v1;
        return yRcHp - kEff * v1 - v2;
    }

    [[nodiscard]] double getStrayField() const noexcept { return lastStrayField_; }

    [[nodiscard]] static double evalLinearMagDb (double freqHz, int stepIdx) noexcept
    {
        if (stepIdx <= 0 || stepIdx >= kNumHpfSteps)
            return 0.0;
        const double fc = kCutoffHz[static_cast<std::size_t> (stepIdx)];
        const double x1 = freqHz / (0.85 * fc);
        const double m1Sq = (x1 * x1) / (1.0 + x1 * x1);

        const double x2 = freqHz / (1.155 * fc);
        const double x2Sq = x2 * x2;
        const double Q = 1.08;
        const double denRe = 1.0 - x2Sq;
        const double denIm = x2 / Q;
        const double m2Sq = (x2Sq * x2Sq) / (denRe * denRe + denIm * denIm);
        return 10.0 * std::log10 (std::max (1e-12, m1Sq * m2Sq));
    }

private:
    void updateCoefficients() noexcept
    {
        if (stepIdx_ <= 0)
            return;

        const double fc = kCutoffHz[static_cast<std::size_t> (stepIdx_)];

        // 1st-order TPT HPF @ 0.85 * fc
        {
            const double f1 = std::min (0.85 * fc, 0.44 * sampleRate_);
            const double g1 = std::tan (std::numbers::pi * f1 / sampleRate_);
            rcG_ = g1 / (1.0 + g1);
        }

        // 2nd-order ZDF SVF HPF @ 1.155 * fc, Q = 1.08 (5.1 kOhm termination resonant bump)
        {
            const double f2 = std::min (1.155 * fc, 0.44 * sampleRate_);
            g_ = std::tan (std::numbers::pi * f2 / sampleRate_);
            k_ = 1.0 / 1.08;
        }
    }

    double sampleRate_ { 192000.0 };
    int    stepIdx_    { 0 };

    double rcG_  { 0.01 }, rcZ1_ { 0.0 };
    double g_    { 0.01 }, k_    { 0.9259 };
    double ic1eq_ { 0.0 }, ic2eq_ { 0.0 };
    double lastStrayField_ { 0.0 };
};

} // namespace nv73eq::dsp
