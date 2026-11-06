#pragma once

#include "B205BaxandallShelves.h"
#include "B211InductorMidBell.h"
#include "BA182InductorHPF.h"
#include <DSP/Oversampler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace nv73eq::dsp
{

struct EQParams
{
    double hfGainDb       { 0.0 };   // Fixed 12 kHz High Shelf: -16 .. +16 dB
    int    midFreqStep    { 3 };     // 0=OFF, 1=360Hz, 2=700Hz, 3=1.6kHz, 4=3.2kHz, 5=4.8kHz, 6=7.2kHz
    double midGainDb      { 0.0 };   // Inductor Mid Bell: -18 .. +18 dB
    int    lowFreqStep    { 2 };     // 0=OFF, 1=35Hz, 2=60Hz, 3=110Hz, 4=220Hz
    double lowGainDb      { 0.0 };   // Polystyrene Low Shelf: -16 .. +16 dB
    int    hpfStep        { 0 };     // 0=OFF, 1=50Hz, 2=80Hz, 3=160Hz, 4=300Hz (18 dB/oct inductor ladder)
    double outputTrimDb   { 0.0 };   // -18 .. +18 dB output level trim
    bool   eqEnabled      { true };  // Hard-wire EQL switch (engages B205 + B211 + BA284 3-band EQ)
    bool   phaseInvert    { false }; // 180-degree polarity flip (Ø)
    nv73::dsp::Oversampler::Factor oversampling { nv73::dsp::Oversampler::Factor::x4 };
};

/**
 * Single-channel Neve 1073LBEQ processor (B205 + B211 + BA182C + BA284).
 */
class Neve1073EQChannel
{
public:
    void prepare (double baseSampleRate) noexcept
    {
        baseSampleRate_ = std::max (22050.0, baseSampleRate);
        outTrimSmoothCoeff_ = 1.0 - std::exp (-1.0 / (0.010 * baseSampleRate_));
        updateOversampledRate (oversampling_);
        reset();
    }

    void reset() noexcept
    {
        oversampler_.reset();
        shelves_.reset();
        midBell_.reset();
        hpf_.reset();
    }

    void updateParameters (const EQParams& p, double railScale = 1.0, bool snap = false) noexcept
    {
        if (p.oversampling != oversampling_)
        {
            oversampling_ = p.oversampling;
            updateOversampledRate (oversampling_);
        }

        eqEnabled_  = p.eqEnabled;
        phaseSign_  = p.phaseInvert ? -1.0 : 1.0;

        shelves_.setParameters (p.hfGainDb, p.lowFreqStep, p.lowGainDb, railScale, snap);
        midBell_.setParameters (p.midFreqStep, p.midGainDb, railScale, snap);
        hpf_.setStepIndex (p.hpfStep);

        outGainTarget_ = std::pow (10.0, p.outputTrimDb / 20.0);
        if (snap)
            outGainCurrent_ = outGainTarget_;
    }

    [[nodiscard]] inline float processSample (float inSample,
                                              double psuRippleVolts,
                                              double inducedCrosstalk,
                                              double& inductorGlowOut,
                                              double& strayFieldOut) noexcept
    {
        outGainCurrent_ += outTrimSmoothCoeff_ * (outGainTarget_ - outGainCurrent_);

        // Subtle inductive pickup on the T1295/T1280 chokes when EQ or HPF is active
        const double xIn = static_cast<double> (inSample) + inducedCrosstalk * 0.22;

        double glowAcc  = 0.0;
        double strayAcc = 0.0;

        const double yOs = oversampler_.processSample (
            xIn,
            oversampling_,
            [this, psuRippleVolts, &glowAcc, &strayAcc] (double s) noexcept -> double
            {
                double v = s;
                if (eqEnabled_)
                {
                    // 1. B205 Active 12 kHz HF & Switched LF Shelves + BA284 Amp 1
                    v = shelves_.processSample (v + psuRippleVolts * 0.008);

                    // 2. B211 Nonlinear Tapped-Inductor Mid Bell + BA284 Amp 2
                    v = midBell_.processSample (v);

                    glowAcc  = std::max (glowAcc, shelves_.getStageSaturation() + midBell_.getInductorStress());
                    strayAcc = midBell_.getStrayField();
                }

                // 3. BA182C Passive 18 dB/Octave Tapped-Inductor High-Pass Filter (independent of EQL switch!)
                v = hpf_.processSample (v);
                strayAcc += hpf_.getStrayField();

                return v;
            });

        inductorGlowOut = glowAcc;
        strayFieldOut   = strayAcc;

        const double out = yOs * outGainCurrent_ * phaseSign_;
        return std::isfinite (out) ? static_cast<float> (out) : 0.0f;
    }

private:
    void updateOversampledRate (nv73::dsp::Oversampler::Factor factor) noexcept
    {
        const double osRate = baseSampleRate_ * static_cast<double> (static_cast<int> (factor));
        shelves_.prepare (osRate);
        midBell_.prepare (osRate);
        hpf_.prepare (osRate);
    }

    double baseSampleRate_ { 48000.0 };
    nv73::dsp::Oversampler::Factor oversampling_ { nv73::dsp::Oversampler::Factor::x4 };

    nv73::dsp::Oversampler oversampler_;
    B205BaxandallShelves   shelves_;
    B211InductorMidBell    midBell_;
    BA182InductorHPF       hpf_;

    bool   eqEnabled_       { true };
    double phaseSign_       { 1.0 };
    double outGainTarget_   { 1.0 };
    double outGainCurrent_  { 1.0 };
    double outTrimSmoothCoeff_ { 0.002 };
};

/**
 * Stereo/Mono Neve 1073LBEQ DSP Engine supporting both Standalone Plugin & LBX-500 Backplane Slot.
 */
class Neve1073EQDSP
{
public:
    static constexpr std::size_t kMaxChannels = 2;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        meterRelCoeff_ = 1.0 - std::exp (-1.0 / (0.045 * sampleRate));
        for (auto& ch : channels_)
            ch.prepare (sampleRate);
    }

    void reset() noexcept
    {
        for (auto& ch : channels_)
            ch.reset();
        inEnv_   = 0.0;
        outEnv_  = 0.0;
        glowEnv_ = 0.0;
    }

    void setParameters (const EQParams& p, double railScale = 1.0, bool snap = false) noexcept
    {
        cachedParams_ = p;
        for (auto& ch : channels_)
            ch.updateParameters (p, railScale, snap);
    }

    void processBlock (float* const* channelPointers, int numChannels, int numSamples) noexcept
    {
        const int chCount = std::min (numChannels, static_cast<int> (kMaxChannels));
        if (chCount <= 0 || numSamples <= 0)
            return;

        double peakIn   = 0.0;
        double peakOut  = 0.0;
        double peakGlow = 0.0;

        for (int ch = 0; ch < chCount; ++ch)
        {
            float* buf = channelPointers[ch];
            if (buf == nullptr)
                continue;

            auto& proc = channels_[static_cast<std::size_t> (ch)];
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = buf[i];
                peakIn = std::max (peakIn, static_cast<double> (std::abs (x)));

                double glow = 0.0, stray = 0.0;
                const float y = proc.processSample (x, 0.0, 0.0, glow, stray);
                buf[i] = y;

                peakOut  = std::max (peakOut,  static_cast<double> (std::abs (y)));
                peakGlow = std::max (peakGlow, glow);
            }
        }

        for (int ch = chCount; ch < numChannels; ++ch)
            if (channelPointers[ch] != nullptr && channelPointers[0] != nullptr)
                std::copy_n (channelPointers[0], numSamples, channelPointers[ch]);

        const double blockRel = std::pow (1.0 - meterRelCoeff_, static_cast<double> (numSamples));
        inEnv_   = std::max (peakIn,   inEnv_   * blockRel);
        outEnv_  = std::max (peakOut,  outEnv_  * blockRel);
        glowEnv_ = std::max (peakGlow, glowEnv_ * blockRel);

        inMeter_.store   (static_cast<float> (inEnv_),   std::memory_order_relaxed);
        outMeter_.store  (static_cast<float> (outEnv_),  std::memory_order_relaxed);
        glowMeter_.store (static_cast<float> (std::clamp (glowEnv_ * 3.5, 0.0, 1.0)), std::memory_order_relaxed);
    }

    /** Single-frame entry point for LBX-500 500-series Lunchbox backplane! */
    inline void processFrameBackplane (float& left,
                                       float& right,
                                       int numChannels,
                                       double railVoltageScale,
                                       double psuRippleVolts,
                                       double inducedCrosstalk,
                                       double& strayFieldOut,
                                       double& dynamicCurrentMaOut) noexcept
    {
        for (auto& ch : channels_)
            ch.updateParameters (cachedParams_, railVoltageScale, false);

        const double peakIn = (numChannels > 1)
            ? std::max (std::abs (static_cast<double> (left)), std::abs (static_cast<double> (right)))
            : std::abs (static_cast<double> (left));

        double glowL = 0.0, strayL = 0.0;
        left = channels_[0].processSample (left, psuRippleVolts, inducedCrosstalk, glowL, strayL);

        double glowR = 0.0, strayR = 0.0;
        if (numChannels > 1)
            right = channels_[1].processSample (right, psuRippleVolts, inducedCrosstalk, glowR, strayR);
        else
            right = left;

        const double peakOut = (numChannels > 1)
            ? std::max (std::abs (static_cast<double> (left)), std::abs (static_cast<double> (right)))
            : std::abs (static_cast<double> (left));
        const double peakGlow = std::max (glowL, glowR);

        const double rel = 1.0 - meterRelCoeff_;
        inEnv_   = std::max (peakIn,   inEnv_   * rel);
        outEnv_  = std::max (peakOut,  outEnv_  * rel);
        glowEnv_ = std::max (peakGlow, glowEnv_ * rel);

        if ((++meterSubsample_ & 63) == 0)
        {
            inMeter_.store   (static_cast<float> (inEnv_),   std::memory_order_relaxed);
            outMeter_.store  (static_cast<float> (outEnv_),  std::memory_order_relaxed);
            glowMeter_.store (static_cast<float> (std::clamp (glowEnv_ * 3.5, 0.0, 1.0)), std::memory_order_relaxed);
        }

        strayFieldOut       = (numChannels > 1) ? 0.5 * (strayL + strayR) : strayL;
        // 1073LBEQ draws 50 mA idle (+16V spec) + up to 18 mA dynamic Class-A / inductor boost drive
        dynamicCurrentMaOut = (cachedParams_.eqEnabled ? 6.0 : 0.0)
                            + std::clamp (peakOut * 8.0 + peakGlow * 16.0, 0.0, 18.0);
    }

    /** Evaluates composite EQ + HPF curve in dB at freqHz for interactive GUI response display. */
    [[nodiscard]] static double evaluateCompositeMagDb (double freqHz, const EQParams& p) noexcept
    {
        double db = p.outputTrimDb;
        if (p.eqEnabled)
        {
            db += B205BaxandallShelves::evalLinearMagDb (freqHz, p.hfGainDb, p.lowFreqStep, p.lowGainDb);
            db += B211InductorMidBell::evalLinearMagDb  (freqHz, p.midFreqStep, p.midGainDb);
        }
        db += BA182InductorHPF::evalLinearMagDb (freqHz, p.hpfStep);
        return db;
    }

    [[nodiscard]] float getInputMeter() const noexcept    { return inMeter_.load (std::memory_order_relaxed); }
    [[nodiscard]] float getOutputMeter() const noexcept   { return outMeter_.load (std::memory_order_relaxed); }
    [[nodiscard]] float getInductorGlow() const noexcept  { return glowMeter_.load (std::memory_order_relaxed); }

private:
    std::array<Neve1073EQChannel, kMaxChannels> channels_;
    EQParams cachedParams_ {};
    double sampleRate_    { 48000.0 };
    double meterRelCoeff_ { 0.001 };

    double inEnv_   { 0.0 };
    double outEnv_  { 0.0 };
    double glowEnv_ { 0.0 };
    std::uint32_t meterSubsample_ { 0 };

    std::atomic<float> inMeter_   { 0.0f };
    std::atomic<float> outMeter_  { 0.0f };
    std::atomic<float> glowMeter_ { 0.0f };
};

} // namespace nv73eq::dsp
