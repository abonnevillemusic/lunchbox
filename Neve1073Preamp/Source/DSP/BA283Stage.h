#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nv73::dsp
{

/**
 * Discrete Single-Ended Class-A 3-Transistor Feedback Amplifier Model
 * Models both:
 *   - BA283NV / BA284 Preamp Stage (TR4, TR5, TR6 = 3x BC184C, R11 = 33k feedback, C8 = 10uF tantalum)
 *   - BA283AM Power Output Stage   (TR1, TR2 = BC184C, TR3 = 2N3055 TO-3 @ ~60mA Class-A bias)
 *
 * Key Circuit Behaviors Modeled:
 * 1. Single-Ended +24V Rail Asymmetric Saturation:
 *    Positive collector excursion (current starvation against R13=5.1k / LO1166 load line) rounds
 *    off more gradually than negative emitter-saturation excursion, generating rich 2nd-order
 *    even harmonics prior to symmetrical clipping.
 * 2. Closed-Loop Negative Feedback Linearization (PL20033 Shunt R_eq & R11 = 33k):
 *    At low closed-loop gain, high loop-gain (1 + beta * A_ol) suppresses distortion and widens
 *    bandwidth; at higher closed-loop gain (e.g. +50 dB single-stage max), reduced feedback lets
 *    warm BJT curvature and Miller capacitance (C9=100pF, C11=680pF) gently contour HF transients.
 * 3. Tantalum Coupling/Emitter Capacitor Dynamic Bias Shift (C8 = 10uF, C1 = 10uF, C5 = 100uF):
 *    Strong asymmetric peaks charge the coupling capacitor with fast attack (~2.2 ms) and smooth
 *    Class-A recovery (~65 ms), momentarily shifting the BJT quiescent operating point ("Neve punch/sag").
 * 4. DC Blocker modelling interstage coupling capacitor (7.5 Hz) to ensure zero DC offset at output.
 */
class BA283Stage
{
public:
    enum class Role
    {
        PreampNV, // BA283NV / BA284 (3x BC184C voltage amplifier)
        OutputAM  // BA283AM (2x BC184C + 2N3055 Class-A output driver)
    };

    explicit BA283Stage (Role role = Role::PreampNV) noexcept
        : role_ (role)
    {
        configureRole (role);
    }

    void configureRole (Role role) noexcept
    {
        role_ = role;
        if (role == Role::PreampNV)
        {
            railScale_      = 1.45;  // Headroom scale of BC184C triple
            nominalBiasU0_  = 0.36;  // Single-ended Class-A quiescent asymmetry -> sweet 2nd-order harmonic
            openLoopGainDb_ = 74.0;  // 3-transistor BC184C open-loop gain (~5000x)
            basePoleHz_     = 145000.0;
            sagAttackMs_    = 2.4;
            sagReleaseMs_   = 58.0;
            sagDepth_       = 0.14;
        }
        else
        {
            // BA283AM: 2N3055 TO-3 single-ended Class-A power driver (~60mA into LO1166 primary)
            railScale_      = 1.25;
            nominalBiasU0_  = 0.44;  // Strong single-ended Class-A 2nd-harmonic bloom before +26 dBu clip
            openLoopGainDb_ = 66.0;
            basePoleHz_     = 95000.0;
            sagAttackMs_    = 1.8;
            sagReleaseMs_   = 82.0;  // C5/C6/C7 = 100uF recovery
            sagDepth_       = 0.18;
        }
        updateParameters();
    }

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::max (22050.0, sampleRate);
        updateParameters();
        reset();
    }

    /**
     * Set closed-loop voltage gain of this discrete block in dB.
     * In 1073 hardware:
     *   - BA283NV runs from ~8 dB up to ~38 dB depending on PL20033 position.
     *   - BA283AM runs at fixed ~16 dB closed-loop feedback ratio.
     */
    void setStageClosedLoopGainDb (double gainDb, bool applyLinearGain = true) noexcept
    {
        closedLoopGainDb_ = std::clamp (gainDb, 0.0, 48.0);
        applyLinearGain_  = applyLinearGain;
        updateParameters();
    }

    /** Adjusts RV1 Class-A bias symmetry trim (-1.0 to +1.0, 0.0 = factory 22.6 Vpp calibration). */
    void setBiasTrim (double trim) noexcept
    {
        biasTrim_ = std::clamp (trim, -1.0, 1.0);
    }

    /** Modulates effective +24V / +/-16V rail headroom when hosted in a sagging 500-series PSU. */
    void setSupplyRailScale (double scale) noexcept
    {
        supplyRailScale_ = std::clamp (scale, 0.65, 1.05);
    }

    void reset() noexcept
    {
        biasSagEnv_ = 0.0;
        hfLpState_  = 0.0;
        dcXPrev_    = 0.0;
        dcYPrev_    = 0.0;
        lastSaturation_ = 0.0;
    }

    [[nodiscard]] inline double processSample (double input) noexcept
    {
        // 1. Apply linear voltage gain of the stage if active in signal path
        const double x = input * (applyLinearGain_ ? linearGain_ : 1.0);

        // 2. Effective single-ended Class-A quiescent operating point u0
        //    Modulated by RV1 bias trimpot and dynamic tantalum coupling-cap (C8/C1/C5) sag envelope
        const double u0 = std::clamp (nominalBiasU0_ + 0.22 * biasTrim_ + sagDepth_ * biasSagEnv_,
                                      0.04, 0.78);

        // 3. Exact-unity-slope single-ended Class-A load-line transfer function:
        //    G(x) = R * cosh^2(u0) * (tanh(x/R + u0) - tanh(u0))
        //    Scaled dynamically by 500-series PSU rail health (supplyRailScale_)!
        const double effectiveRail = railScale_ * supplyRailScale_;
        const double shaped = singleEndedClassATransfer (x, effectiveRail, u0);

        // Negative feedback (R11 = 33k / R_eq) linearizes small/medium-signal curvature in proportion
        // to loop-gain margin while still hitting the physical rail ceiling cleanly.
        const double yNonlinear = feedbackLinearize (x, shaped, effectiveRail, u0);

        // 4. Update coupling capacitor (C8 / C1 / C5) rectified charge envelope (bias sag)
        const double compressionDelta = std::abs (x - yNonlinear);
        const double coeff = (compressionDelta > biasSagEnv_) ? sagAttCoeff_ : sagRelCoeff_;
        biasSagEnv_ += coeff * (compressionDelta - biasSagEnv_);
        lastSaturation_ = compressionDelta;

        // 5. Gain-dependent Miller compensation HF pole (C9 = 100pF, C11 = 680pF)
        hfLpState_ += hfAlpha_ * (yNonlinear - hfLpState_);

        // 6. Interstage tantalum coupling capacitor DC blocker (~7.5 Hz) so rectified even-harmonic DC never leaks
        const double dcOut = hfLpState_ - dcXPrev_ + dcR_ * dcYPrev_;
        dcXPrev_ = hfLpState_;
        dcYPrev_ = dcOut;

        return dcOut;
    }

    [[nodiscard]] double getSaturationAmount() const noexcept { return lastSaturation_; }

private:
    [[nodiscard]] static inline double singleEndedClassATransfer (double x, double railR, double u0) noexcept
    {
        const double t0 = std::tanh (u0);
        const double coshSq = 1.0 / (1.0 - t0 * t0); // Exact sech^-2(u0) = cosh^2(u0)
        return railR * coshSq * (std::tanh ((x / railR) + u0) - t0);
    }

    [[nodiscard]] inline double feedbackLinearize (double vIdeal, double vRawBjt, double effectiveRail, double u0) const noexcept
    {
        // High NFB loop-gain reduces quiescent asymmetry u0, while keeping physical rail saturation intact
        const double vFeedbackClamped = singleEndedClassATransfer (vIdeal, effectiveRail * 1.15, u0 * 0.30);
        return feedbackLinearity_ * vFeedbackClamped + (1.0 - feedbackLinearity_) * vRawBjt;
    }

    void updateParameters() noexcept
    {
        linearGain_ = std::pow (10.0, closedLoopGainDb_ / 20.0);

        // Loop gain margin in dB = A_open_loop_dB - A_closed_loop_dB
        const double loopMarginDb = std::max (6.0, openLoopGainDb_ - closedLoopGainDb_);
        // Map loop margin [6 dB .. 66 dB] -> feedback linearity [0.12 .. 0.82]
        feedbackLinearity_ = std::clamp ((loopMarginDb - 6.0) / 72.0, 0.12, 0.82);

        // Closed-loop HF bandwidth scales with feedback factor (gain-bandwidth product of C9/C11)
        const double loopFactor = std::pow (10.0, loopMarginDb / 35.0);
        const double cutoffHz = std::clamp (basePoleHz_ * (0.22 + 0.12 * loopFactor),
                                            24000.0,
                                            0.46 * sampleRate_);
        const double w = (2.0 * std::numbers::pi * cutoffHz) / sampleRate_;
        hfAlpha_ = w / (1.0 + w);

        // Envelope attack / release coefficients
        sagAttCoeff_ = 1.0 - std::exp (-1.0 / (0.001 * sagAttackMs_ * sampleRate_));
        sagRelCoeff_ = 1.0 - std::exp (-1.0 / (0.001 * sagReleaseMs_ * sampleRate_));

        // 7.5 Hz coupling cap DC blocker
        dcR_ = std::exp (-(2.0 * std::numbers::pi * 7.5) / sampleRate_);
    }

    Role   role_ { Role::PreampNV };
    double sampleRate_ { 48000.0 };

    double railScale_      { 1.45 };
    double nominalBiasU0_  { 0.36 };
    double openLoopGainDb_ { 74.0 };
    double basePoleHz_     { 145000.0 };
    double sagAttackMs_    { 2.4 };
    double sagReleaseMs_   { 58.0 };
    double sagDepth_       { 0.11 };

    double closedLoopGainDb_  { 16.0 };
    bool   applyLinearGain_   { false };
    double biasTrim_          { 0.0 };
    double supplyRailScale_   { 1.0 };
    double linearGain_        { 1.0 };
    double feedbackLinearity_ { 0.65 };

    double hfAlpha_     { 0.8 };
    double sagAttCoeff_ { 0.01 };
    double sagRelCoeff_ { 0.0005 };
    double dcR_         { 0.999 };

    // Runtime states
    double biasSagEnv_ { 0.0 };
    double hfLpState_  { 0.0 };
    double dcXPrev_    { 0.0 };
    double dcYPrev_    { 0.0 };
    double lastSaturation_ { 0.0 };
};

} // namespace nv73::dsp
