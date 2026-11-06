#pragma once

#include <DSP/BA283Stage.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>

namespace nv73eq::dsp
{

/**
 * B211 (Drawing D10048) Multi-Tapped Inductor LCR Presence / Midrange Peaking Section
 * + BA284 Amplifier 2 (3x BC184C discrete Class-A stage).
 *
 * Models:
 *   - 6 Authentic Tapped-Inductor Frequencies + OFF:
 *       360 Hz, 700 Hz         -> T1295 (VTB9043) 10H / 3H / 1.3H iron-core taps (chewier core saturation)
 *       1.6k, 3.2k, 4.8k, 7.2k -> T1280 (VTB9044) 200mH..68mH presence taps (focused bite, sweet coil R_dc)
 *   - +/-18 dB Boost/Cut with authentic Proportional-Q tightening (~0.68 at +/-2 dB -> ~2.15 at +/-18 dB)
 *   - Nonlinear Inductor Tank Core Saturation: High coil current i_L inside boosted bands saturates the
 *     magnetic core smoothly, dynamically taming harsh resonant peaks while radiating stray magnetic flux!
 */
class B211InductorMidBell
{
public:
    struct InductorTapSpec
    {
        std::string_view label;
        double centerHz;
        double tapQScale;      // Tap-specific inductor Q factor (coil DCR & inductance/capacitance ratio)
        double coreSatFactor;  // T1295 iron-core low-mid taps saturate warmer than T1280 upper-mid taps
    };

    static constexpr int kNumMidSteps = 7;
    static constexpr std::array<InductorTapSpec, kNumMidSteps> kMidTaps {{
        { "OFF",    0.0, 1.00, 0.00 },
        { "0.36", 360.0, 0.88, 0.95 }, // T1295 ~3.0H tap + 65nF polystyrene
        { "0.7",  700.0, 0.94, 0.80 }, // T1295 ~1.3H tap + 39nF polystyrene
        { "1.6", 1600.0, 1.08, 0.58 }, // T1280 200mH full coil + 49nF polystyrene
        { "3.2", 3200.0, 1.12, 0.48 }, // T1280 160mH tap + 15.5nF polystyrene
        { "4.8", 4800.0, 1.02, 0.42 }, // T1280 100mH tap + 11nF polystyrene
        { "7.2", 7200.0, 0.90, 0.36 }  // T1280 ~68mH tap + 7.2nF polystyrene
    }};

    B211InductorMidBell() noexcept
        : ba284Amp2_ (nv73::dsp::BA283Stage::Role::PreampNV)
    {
        ba284Amp2_.setStageClosedLoopGainDb (12.0, false);
    }

    void prepare (double sampleRate) noexcept
    {
        sampleRate_  = std::max (22050.0, sampleRate);
        smoothCoeff_ = 1.0 - std::exp (-1.0 / (0.008 * sampleRate_));
        ba284Amp2_.prepare (sampleRate_);
        updateBellCoefficients();
        reset();
    }

    void reset() noexcept
    {
        ic1eq_ = 0.0;
        ic2eq_ = 0.0;
        lastInductorStress_ = 0.0;
        lastStrayField_     = 0.0;
        ba284Amp2_.reset();
    }

    void setParameters (int midStepIndex, double midGainDb, double railScale, bool snap = false) noexcept
    {
        midStepIdx_  = std::clamp (midStepIndex, 0, kNumMidSteps - 1);
        midTargetDb_ = (midStepIdx_ == 0) ? 0.0 : std::clamp (midGainDb, -18.0, 18.0);

        ba284Amp2_.setSupplyRailScale (railScale);
        ba284Amp2_.setStageClosedLoopGainDb (10.0 + 0.75 * std::max (0.0, midTargetDb_), false);

        if (snap)
        {
            midCurrentDb_ = midTargetDb_;
            updateBellCoefficients();
        }
        else if (midStepIdx_ != prevStepIdx_)
        {
            prevStepIdx_ = midStepIdx_;
            updateBellCoefficients();
        }
    }

    [[nodiscard]] inline double processSample (double x) noexcept
    {
        if (std::abs (midTargetDb_ - midCurrentDb_) > 1e-4)
        {
            midCurrentDb_ += smoothCoeff_ * (midTargetDb_ - midCurrentDb_);
            if ((++coeffSubdiv_ & 15) == 0)
                updateBellCoefficients();
        }

        double v = x;
        if (midStepIdx_ > 0)
        {
            const auto& tap = kMidTaps[static_cast<std::size_t> (midStepIdx_)];

            // Cytomic ZDF Peaking Bell SVF with sample-rate-invariant nonlinear T1295/T1280 core saturation:
            // High instantaneous coil current i_L increases effective magnetic core loss k_eff(i_L),
            // staying <0.05 dB linear at small signals while smoothly taming hot resonant peaks.
            const double iPrevSq = ic1eq_ * ic1eq_;
            const double satAmt  = tap.coreSatFactor * 0.18;
            const double kEff    = k_ * (1.0 + satAmt * iPrevSq);
            const double a1      = 1.0 / (1.0 + g_ * (g_ + kEff));
            const double a2      = g_ * a1;
            const double a3      = g_ * a2;

            const double v3  = v - ic2eq_;
            const double v1  = a1 * ic1eq_ + a2 * v3; // Normalized inductor coil current i_L
            const double v2  = ic2eq_ + a2 * ic1eq_ + a3 * v3;

            ic1eq_ = 2.0 * v1 - ic1eq_;
            ic2eq_ = 2.0 * v2 - ic2eq_;

            // Soft B-H curve compression on the tapped inductor voltage contribution at high drive
            const double v1Core = v1 / std::sqrt (1.0 + (tap.coreSatFactor * 0.035) * v1 * v1);
            v = v + m1_ * v1Core;

            const double nonlinDelta = std::abs (v1 - v1Core) + std::abs (v1) * (satAmt * iPrevSq) * 0.15;
            lastInductorStress_ = nonlinDelta * 3.5;
            lastStrayField_     = 0.18 * v1Core + 2.0 * (v1 - v1Core);
        }
        else
        {
            lastInductorStress_ = 0.0;
            lastStrayField_     = 0.0;
        }

        return ba284Amp2_.processSample (v);
    }

    [[nodiscard]] double getInductorStress() const noexcept { return lastInductorStress_ + ba284Amp2_.getSaturationAmount(); }
    [[nodiscard]] double getStrayField() const noexcept     { return lastStrayField_; }

    [[nodiscard]] static double computeEffectiveQ (int stepIdx, double gainDb) noexcept
    {
        if (stepIdx <= 0 || stepIdx >= kNumMidSteps)
            return 1.0;
        const double tapScale = kMidTaps[static_cast<std::size_t> (stepIdx)].tapQScale;
        const double normGain = std::pow (std::abs (gainDb) / 18.0, 0.85);
        return tapScale * (0.68 + 1.42 * normGain);
    }

    [[nodiscard]] static double evalLinearMagDb (double freqHz, int stepIdx, double gainDb) noexcept
    {
        if (stepIdx <= 0 || stepIdx >= kNumMidSteps || std::abs (gainDb) < 0.01)
            return 0.0;
        const double f0 = kMidTaps[static_cast<std::size_t> (stepIdx)].centerHz;
        const double Q  = computeEffectiveQ (stepIdx, gainDb);
        const double A  = std::pow (10.0, gainDb / 40.0);
        // Analog peaking EQ: H(s) = (s^2 + (A/Q)*s + 1) / (s^2 + (1/(A*Q))*s + 1), s = j * f / f0
        const double x  = freqHz / f0;
        const double d  = 1.0 - x * x;
        const double num2 = d * d + (A / Q) * (A / Q) * x * x;
        const double den2 = d * d + (1.0 / (A * Q)) * (1.0 / (A * Q)) * x * x;
        return 10.0 * std::log10 (num2 / den2);
    }

private:
    void updateBellCoefficients() noexcept
    {
        if (midStepIdx_ <= 0)
            return;

        const auto& tap = kMidTaps[static_cast<std::size_t> (midStepIdx_)];
        const double fc = std::min (tap.centerHz, 0.44 * sampleRate_);
        const double Q  = computeEffectiveQ (midStepIdx_, midCurrentDb_);
        const double A  = std::pow (10.0, midCurrentDb_ / 40.0);

        g_  = std::tan (std::numbers::pi * fc / sampleRate_);
        k_  = 1.0 / (Q * A);
        m1_ = k_ * (A * A - 1.0);
    }

    double sampleRate_  { 192000.0 };
    double smoothCoeff_ { 0.005 };

    int    midStepIdx_   { 3 }; // Default: 1.6 kHz
    int    prevStepIdx_  { 3 };
    double midTargetDb_  { 0.0 };
    double midCurrentDb_ { 0.0 };
    std::uint32_t coeffSubdiv_ { 0 };

    double g_ { 0.01 }, k_ { 1.0 }, m1_ { 0.0 };
    double ic1eq_ { 0.0 }, ic2eq_ { 0.0 };

    double lastInductorStress_ { 0.0 };
    double lastStrayField_     { 0.0 };

    nv73::dsp::BA283Stage ba284Amp2_;
};

} // namespace nv73eq::dsp
