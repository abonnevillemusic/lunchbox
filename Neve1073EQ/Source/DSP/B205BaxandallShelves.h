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
 * B205 (Drawing D10042) Active Baxandall High & Low Frequency Shelving Network
 * wrapped around BA284 Amplifier 1 (3x BC184C discrete single-ended Class-A stage).
 *
 * - High Shelf: Fixed 12 kHz, +/-16 dB (Q = 0.56 smooth Baxandall RC feedback curve)
 * - Low Shelf : Stepped OFF / 35 Hz / 60 Hz / 110 Hz / 220 Hz, +/-16 dB (Q = 0.66 classic Neve transition)
 * - Active Stage: Boosting shelves reduces closed-loop NFB margin in BA284 Amp 1, adding subtle
 *   even-order BC184C harmonic sheen proportional to boost demand.
 */
class B205BaxandallShelves
{
public:
    static constexpr int kNumLowFreqs = 5;
    static constexpr std::array<double, kNumLowFreqs> kLowFreqsHz { 0.0, 35.0, 60.0, 110.0, 220.0 };
    static constexpr std::array<std::string_view, kNumLowFreqs> kLowLabels { "OFF", "35", "60", "110", "220" };

    B205BaxandallShelves() noexcept
        : ba284Amp1_ (nv73::dsp::BA283Stage::Role::PreampNV)
    {
        ba284Amp1_.setStageClosedLoopGainDb (12.0, false);
    }

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::max (22050.0, sampleRate);
        smoothCoeff_ = 1.0 - std::exp (-1.0 / (0.008 * sampleRate_));
        ba284Amp1_.prepare (sampleRate_);
        updateShelfCoefficients (true);
        reset();
    }

    void reset() noexcept
    {
        hfIc1eq_ = 0.0;
        hfIc2eq_ = 0.0;
        lfIc1eq_ = 0.0;
        lfIc2eq_ = 0.0;
        ba284Amp1_.reset();
    }

    void setParameters (double hfGainDb, int lfStepIndex, double lfGainDb, double railScale, bool snap = false) noexcept
    {
        hfTargetDb_ = std::clamp (hfGainDb, -16.0, 16.0);
        lfStepIdx_  = std::clamp (lfStepIndex, 0, kNumLowFreqs - 1);
        lfTargetDb_ = (lfStepIdx_ == 0) ? 0.0 : std::clamp (lfGainDb, -16.0, 16.0);

        ba284Amp1_.setSupplyRailScale (railScale);

        // Boosting shelves inside the BA284 feedback loop reduces effective loop-gain margin
        const double activeBoostDemand = std::max (0.0, hfTargetDb_) + std::max (0.0, lfTargetDb_);
        ba284Amp1_.setStageClosedLoopGainDb (10.0 + 0.85 * activeBoostDemand, false);

        if (snap)
        {
            hfCurrentDb_ = hfTargetDb_;
            lfCurrentDb_ = lfTargetDb_;
            updateShelfCoefficients (true);
        }
    }

    [[nodiscard]] inline double processSample (double x) noexcept
    {
        // Smoothly slew shelf dB gains if moving to avoid any zipper clicks
        if (std::abs (hfTargetDb_ - hfCurrentDb_) > 1e-4 || std::abs (lfTargetDb_ - lfCurrentDb_) > 1e-4)
        {
            hfCurrentDb_ += smoothCoeff_ * (hfTargetDb_ - hfCurrentDb_);
            lfCurrentDb_ += smoothCoeff_ * (lfTargetDb_ - lfCurrentDb_);
            if ((++coeffUpdateSubdiv_ & 15) == 0)
                updateShelfCoefficients (false);
        }

        // 1. 12 kHz High Shelf (Cytomic ZDF SVF High Shelf)
        double v = x;
        {
            const double v3 = v - hfIc2eq_;
            const double v1 = hfA1_ * hfIc1eq_ + hfA2_ * v3;
            const double v2 = hfIc2eq_ + hfA2_ * hfIc1eq_ + hfA3_ * v3;
            hfIc1eq_ = 2.0 * v1 - hfIc1eq_;
            hfIc2eq_ = 2.0 * v2 - hfIc2eq_;
            v = hfM0_ * v + hfM1_ * v1 + hfM2_ * v2;
        }

        // 2. Switched Polystyrene Low Shelf (Cytomic ZDF SVF Low Shelf)
        if (lfStepIdx_ > 0)
        {
            const double v3 = v - lfIc2eq_;
            const double v1 = lfA1_ * lfIc1eq_ + lfA2_ * v3;
            const double v2 = lfIc2eq_ + lfA2_ * lfIc1eq_ + lfA3_ * v3;
            lfIc1eq_ = 2.0 * v1 - lfIc1eq_;
            lfIc2eq_ = 2.0 * v2 - lfIc2eq_;
            v = lfM0_ * v + lfM1_ * v1 + lfM2_ * v2;
        }

        // 3. BA284 Discrete 3x BC184C Class-A Summing/Feedback Amplifier
        return ba284Amp1_.processSample (v);
    }

    [[nodiscard]] double getStageSaturation() const noexcept { return ba284Amp1_.getSaturationAmount(); }

    static constexpr double kHfInflectionHz = 7800.0; // B205 "12 kHz" RC network reaches plateau knee at 12-18 kHz

    /** Exact linear magnitude^2 evaluation at freqHz for the real-time UI curve display. */
    [[nodiscard]] static double evalLinearMagDb (double freqHz, double hfGainDb, int lfStepIdx, double lfGainDb) noexcept
    {
        double totalDb = 0.0;
        if (std::abs (hfGainDb) > 0.01)
            totalDb += analogShelfMagDb (freqHz, kHfInflectionHz, 0.56, hfGainDb, true);
        if (lfStepIdx > 0 && lfStepIdx < kNumLowFreqs && std::abs (lfGainDb) > 0.01)
            totalDb += analogShelfMagDb (freqHz, kLowFreqsHz[static_cast<std::size_t> (lfStepIdx)], 0.66, lfGainDb, false);
        return totalDb;
    }

private:
    static double analogShelfMagDb (double f, double fc, double Q, double gainDb, bool isHigh) noexcept
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double x  = f / std::max (1.0, fc);
        const double x2 = x * x;
        const double k2 = A / (Q * Q);
        if (! isHigh)
        {
            // H_ls(s) = A * (s^2 + (sqrt(A)/Q)*s + A) / (A*s^2 + (sqrt(A)/Q)*s + 1), s = j*f/fc
            const double num2 = (A - x2) * (A - x2) + k2 * x2;
            const double den2 = (1.0 - A * x2) * (1.0 - A * x2) + k2 * x2;
            return 10.0 * std::log10 ((A * A * num2) / den2);
        }

        // H_hs(s) = A * (A*s^2 + (sqrt(A)/Q)*s + 1) / (s^2 + (sqrt(A)/Q)*s + A), s = j*f/fc
        const double num2 = (1.0 - A * x2) * (1.0 - A * x2) + k2 * x2;
        const double den2 = (A - x2) * (A - x2) + k2 * x2;
        return 10.0 * std::log10 ((A * A * num2) / den2);
    }

    void updateShelfCoefficients (bool /*force*/) noexcept
    {
        // Cytomic ZDF High Shelf ("12 kHz" B205 RC shelf, inflection @ 7.8 kHz, Q = 0.56)
        {
            const double fc = std::min (kHfInflectionHz, 0.44 * sampleRate_);
            const double A  = std::pow (10.0, hfCurrentDb_ / 40.0);
            const double g  = std::tan (std::numbers::pi * fc / sampleRate_) * std::sqrt (A);
            const double k  = 1.0 / 0.56;
            hfA1_ = 1.0 / (1.0 + g * (g + k));
            hfA2_ = g * hfA1_;
            hfA3_ = g * hfA2_;
            hfM0_ = A * A;
            hfM1_ = k * (1.0 - A) * A;
            hfM2_ = 1.0 - A * A;
        }

        // Cytomic ZDF Low Shelf @ kLowFreqsHz[lfStepIdx_] (Q = 0.66)
        if (lfStepIdx_ > 0)
        {
            const double fc = std::min (kLowFreqsHz[static_cast<std::size_t> (lfStepIdx_)], 0.44 * sampleRate_);
            const double A  = std::pow (10.0, lfCurrentDb_ / 40.0);
            const double g  = std::tan (std::numbers::pi * fc / sampleRate_) / std::sqrt (A);
            const double k  = 1.0 / 0.66;
            lfA1_ = 1.0 / (1.0 + g * (g + k));
            lfA2_ = g * lfA1_;
            lfA3_ = g * lfA2_;
            lfM0_ = 1.0;
            lfM1_ = k * (A - 1.0);
            lfM2_ = A * A - 1.0;
        }
    }

    double sampleRate_  { 192000.0 };
    double smoothCoeff_ { 0.005 };

    double hfTargetDb_ { 0.0 }, hfCurrentDb_ { 0.0 };
    int    lfStepIdx_  { 2 }; // Default: 60 Hz
    double lfTargetDb_ { 0.0 }, lfCurrentDb_ { 0.0 };
    std::uint32_t coeffUpdateSubdiv_ { 0 };

    // ZDF SVF coefficients & states
    double hfA1_ { 0.0 }, hfA2_ { 0.0 }, hfA3_ { 0.0 }, hfM0_ { 1.0 }, hfM1_ { 0.0 }, hfM2_ { 0.0 };
    double hfIc1eq_ { 0.0 }, hfIc2eq_ { 0.0 };

    double lfA1_ { 0.0 }, lfA2_ { 0.0 }, lfA3_ { 0.0 }, lfM0_ { 1.0 }, lfM1_ { 0.0 }, lfM2_ { 0.0 };
    double lfIc1eq_ { 0.0 }, lfIc2eq_ { 0.0 };

    nv73::dsp::BA283Stage ba284Amp1_;
};

} // namespace nv73eq::dsp
