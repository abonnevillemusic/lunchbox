#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nv73::dsp
{

/**
 * Physical Virtual-Analog Audio Transformer Model (Marinair T10468 & Gapped LO1166).
 *
 * Models:
 * 1. Shunt nonlinear magnetizing inductance L_m(Phi) fed via primary source/winding impedance:
 *      d(phi)/dt = wc * v_core,   v_core = v_in - i_m(phi)
 *      i_m(phi)  = (phi + dcBias) + cSat * (phi + dcBias)^3 - (dcBias + cSat * dcBias^3)
 *    Solved implicitly per sample via 3-step Newton-Raphson (strictly monotonic Jacobian > 1,
 *    unconditionally stable at all sample rates and drive levels).
 *    Naturally produces:
 *      - Frequency-dependent core saturation (Phi ~ V / f, enriching 30-150 Hz bass weight)
 *      - Even-order (2nd) harmonics when dcBias > 0 (60mA DC-biased gapped LO1166 output iron)
 *      - Odd-order (3rd) permalloy core harmonics in T10468 input iron
 * 2. Coupled AC high-pass / LC "head-bump" resonance (~38-45 Hz subtle +0.5 dB contour)
 * 3. Secondary leakage inductance + distributed winding capacitance + Zobel network (ZDF SVF)
 *    producing the classic ~19.5 kHz T10468 "air" lift and smooth ultrasonic roll-off.
 */
class TransformerModel
{
public:
    enum class Type
    {
        MarinairT10468Input, // Dual-bobbin permalloy mic input transformer
        MarinairLO1166Output // Gapped steel Class-A output transformer (~60 mA DC bias)
    };

    struct Config
    {
        double primaryHpHz   { 16.5 };  // Primary magnetizing R/L cutoff (~16.5 Hz)
        double satCoeff      { 0.35 };  // Nonlinear core reluctance coefficient
        double dcFluxBias    { 0.04 };  // Static core DC offset (small remanence on T10468; heavy ~0.42 on LO1166)
        double headBumpHz    { 42.0 };  // Coupling cap + primary L resonance frequency
        double headBumpQ     { 0.79 };  // Q > 0.7071 creates subtle ~0.55 dB analog head bump
        double secResHz      { 19800.0 }; // Secondary leakage L + winding C resonance
        double secResQ       { 0.82 };  // Zobel-damped HF resonance Q
        double eddyLossMix   { 0.12 };  // High-frequency core eddy-current smoothing
    };

    explicit TransformerModel (Type type = Type::MarinairT10468Input) noexcept
        : type_ (type)
    {
        configureForType (type, false);
    }

    void configureForType (Type type, bool loZImpedance) noexcept
    {
        type_ = type;
        loZ_  = loZImpedance;

        if (type == Type::MarinairT10468Input)
        {
            // T10468: Permalloy core; Lo-Z (300 ohm parallel primary) drives core harder with slightly tighter HF damping
            // Normalized flux phi = (f_hp / f) * V: at 55 Hz, phi ~ 0.27 * V, so satCoeff ~ 14-20 models +20 dBu @ 50 Hz core knee!
            cfg_.primaryHpHz = loZImpedance ? 18.5 : 15.0;
            cfg_.satCoeff    = loZImpedance ? 55.0 : 36.0;
            cfg_.dcFluxBias  = 0.025; // Subtle permalloy remanence asymmetry
            cfg_.headBumpHz  = loZImpedance ? 46.0 : 38.0;
            cfg_.headBumpQ   = 0.78;
            cfg_.secResHz    = loZImpedance ? 18200.0 : 20500.0;
            cfg_.secResQ     = loZImpedance ? 0.76 : 0.86;
            cfg_.eddyLossMix = 0.10;
        }
        else
        {
            // LO1166: Heavy gapped steel core carrying ~60 mA continuous Class-A 2N3055 collector current
            cfg_.primaryHpHz = 14.5;
            cfg_.satCoeff    = 62.0;
            cfg_.dcFluxBias  = 0.075; // 60 mA DC bias shifts B-H operating point -> rich low-end 2nd + 3rd iron bloom!
            cfg_.headBumpHz  = 36.0;
            cfg_.headBumpQ   = 0.80;
            cfg_.secResHz    = 22500.0;
            cfg_.secResQ     = 0.72;
            cfg_.eddyLossMix = 0.15;
        }

        updateCoefficients();
    }

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::max (22050.0, sampleRate);
        updateCoefficients();
        reset();
    }

    void setIronDriveScale (double scale) noexcept
    {
        ironScale_ = std::clamp (scale, 0.1, 4.0);
    }

    void reset() noexcept
    {
        phi_      = 0.0;
        vCorePrev_ = 0.0;
        hbIc1eq_  = 0.0;
        hbIc2eq_  = 0.0;
        secIc1eq_ = 0.0;
        secIc2eq_ = 0.0;
        eddyState_ = 0.0;
        lastCoreStress_ = 0.0;
    }

    [[nodiscard]] inline double processSample (double vIn) noexcept
    {
        // Guard astronomical inputs smoothly while preserving headroom
        const double x = std::clamp (vIn, -16.0, 16.0);

        // 1. Implicit trapezoidal / backward-Euler nonlinear magnetizing flux solver:
        //    phi_n - phi_{n-1} = dt_wc * (x - i_m(phi_n))
        const double cSat = cfg_.satCoeff * ironScale_;
        const double b    = cfg_.dcFluxBias;
        const double iBias = b + cSat * b * b * b;

        double phiNext = phi_;
        for (int iter = 0; iter < 3; ++iter)
        {
            const double pb   = phiNext + b;
            const double pb2  = pb * pb;
            const double iMag = (pb + cSat * pb * pb2) - iBias;
            const double dIdPhi = 1.0 + 3.0 * cSat * pb2;

            const double residual = phiNext - phi_ - dtWc_ * (x - iMag);
            const double jacobian = 1.0 + dtWc_ * dIdPhi;
            phiNext -= residual / jacobian;
        }

        const double pb   = phiNext + b;
        const double iMag = (pb + cSat * pb * pb * pb) - iBias;
        const double vCore = x - iMag;

        // Track magnetic core stress & signed radiated stray field for UI + 500-series adjacent crosstalk
        const double nonlinTerm = cSat * pb * pb * pb - cSat * b * b * b;
        lastCoreStress_  = std::abs (nonlinTerm);
        lastStrayField_  = 0.35 * vCore + 1.65 * nonlinTerm;

        phi_ = phiNext;
        vCorePrev_ = vCore;

        // 2. Eddy-current high-frequency core loss (subtle shelf/damping of harsh upper harmonics)
        eddyState_ += eddyAlpha_ * (vCore - eddyState_);
        const double vDamped = vCore * (1.0 - cfg_.eddyLossMix) + eddyState_ * cfg_.eddyLossMix;

        // 3. Coupling capacitor + primary inductance resonant high-pass ("Neve head bump")
        const double vHeadBump = processHighPassSVF (vDamped);

        // 4. Secondary leakage inductance + winding capacitance + Zobel network low-pass SVF
        return processLowPassSVF (vHeadBump);
    }

    [[nodiscard]] double getCoreStress() const noexcept  { return lastCoreStress_; }
    [[nodiscard]] double getStrayField() const noexcept  { return lastStrayField_; }

private:
    void updateCoefficients() noexcept
    {
        dtWc_ = (2.0 * std::numbers::pi * cfg_.primaryHpHz) / sampleRate_;

        // Eddy-current LP pole around ~9 kHz
        const double eddyW = (2.0 * std::numbers::pi * std::min (9000.0, 0.42 * sampleRate_)) / sampleRate_;
        eddyAlpha_ = eddyW / (1.0 + eddyW);

        // Cytomic ZDF SVF coefficients for subsonic LC head-bump high-pass
        {
            const double f = std::min (cfg_.headBumpHz, 0.45 * sampleRate_);
            const double g = std::tan (std::numbers::pi * f / sampleRate_);
            const double k = 1.0 / cfg_.headBumpQ;
            hbA1_ = 1.0 / (1.0 + g * (g + k));
            hbA2_ = g * hbA1_;
            hbA3_ = g * hbA2_;
            hbK_  = k;
        }

        // Cytomic ZDF SVF coefficients for secondary leakage/capacitance resonance low-pass
        {
            const double f = std::min (cfg_.secResHz, 0.46 * sampleRate_);
            const double g = std::tan (std::numbers::pi * f / sampleRate_);
            const double k = 1.0 / cfg_.secResQ;
            secA1_ = 1.0 / (1.0 + g * (g + k));
            secA2_ = g * secA1_;
            secA3_ = g * secA2_;
        }
    }

    [[nodiscard]] inline double processHighPassSVF (double v0) noexcept
    {
        const double v3 = v0 - hbIc2eq_;
        const double v1 = hbA1_ * hbIc1eq_ + hbA2_ * v3;
        const double v2 = hbIc2eq_ + hbA2_ * hbIc1eq_ + hbA3_ * v3;
        hbIc1eq_ = 2.0 * v1 - hbIc1eq_;
        hbIc2eq_ = 2.0 * v2 - hbIc2eq_;
        return v0 - hbK_ * v1 - v2; // Exact HP output of ZDF SVF
    }

    [[nodiscard]] inline double processLowPassSVF (double v0) noexcept
    {
        const double v3 = v0 - secIc2eq_;
        const double v1 = secA1_ * secIc1eq_ + secA2_ * v3;
        const double v2 = secIc2eq_ + secA2_ * secIc1eq_ + secA3_ * v3;
        secIc1eq_ = 2.0 * v1 - secIc1eq_;
        secIc2eq_ = 2.0 * v2 - secIc2eq_;
        return v2; // Exact LP output of ZDF SVF
    }

    Type   type_ { Type::MarinairT10468Input };
    bool   loZ_  { false };
    Config cfg_  {};

    double sampleRate_ { 48000.0 };
    double ironScale_  { 1.0 };

    double dtWc_      { 0.002 };
    double eddyAlpha_ { 0.5 };

    // ZDF SVF coefficients & states
    double hbA1_ { 0.0 }, hbA2_ { 0.0 }, hbA3_ { 0.0 }, hbK_ { 1.3 };
    double hbIc1eq_ { 0.0 }, hbIc2eq_ { 0.0 };

    double secA1_ { 0.0 }, secA2_ { 0.0 }, secA3_ { 0.0 };
    double secIc1eq_ { 0.0 }, secIc2eq_ { 0.0 };

    // Flux & eddy states
    double phi_       { 0.0 };
    double vCorePrev_ { 0.0 };
    double eddyState_ { 0.0 };
    double lastCoreStress_ { 0.0 };
    double lastStrayField_ { 0.0 };
};

} // namespace nv73::dsp
