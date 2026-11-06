#pragma once

#include "BA283Stage.h"
#include "GainSwitchPL20033.h"
#include "Oversampler.h"
#include "TransformerModel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace nv73::dsp
{

/**
 * Parameter snapshot passed smoothly to the Neve 1073 Preamp DSP Engine.
 */
struct PreampParams
{
    int   gainStepIndex    { 3 };     // 0 = LINE, 1..13 = 20..80 dB in 5 dB steps (default 3 = 30 dB)
    double faderDriveDb    { 0.0 };   // Interstage channel fader before BA283AM (-24 dB .. +12 dB)
    double outputTrimDb    { 0.0 };   // Post-LO1166 output pad/trim (-36 dB .. +12 dB)
    double biasTrim        { 0.0 };   // RV1 2N3055 Class-A bias calibration (-1.0 .. +1.0)
    bool  loZImpedance     { false }; // False = 1200 ohm Hi-Z, True = 300 ohm Lo-Z (+2.5 dB step-up & harder core load)
    bool  phaseInvert      { false }; // 180-degree polarity flip (Ø)
    bool  autoGainComp     { true };  // DAW-friendly loudness compensation when sweeping Red Gain Knob
    bool  analogNoise      { false }; // Authentic -125.5 dBu EIN transistor thermal hiss
    Oversampler::Factor oversampling { Oversampler::Factor::x4 };
};

/**
 * Single-channel smoothing helper to eliminate zipper noise on continuous controls.
 */
class OnePoleSmoother
{
public:
    void prepare (double sampleRate, double timeMs = 12.0) noexcept
    {
        const double samples = std::max (1.0, 0.001 * timeMs * sampleRate);
        coeff_ = 1.0 - std::exp (-1.0 / samples);
    }

    void reset (double value) noexcept
    {
        current_ = value;
        target_  = value;
    }

    void setTarget (double target) noexcept { target_ = target; }

    [[nodiscard]] inline double getNext() noexcept
    {
        current_ += coeff_ * (target_ - current_);
        return current_;
    }

private:
    double coeff_   { 0.01 };
    double current_ { 1.0 };
    double target_  { 1.0 };
};

/**
 * Fast, deterministic PRNG for subtle analog thermal EIN noise (zero library state, lock-free).
 */
class WhiteNoiseGen
{
public:
    explicit WhiteNoiseGen (std::uint32_t seed = 0x1073BA28u) noexcept : state_ (seed) {}

    [[nodiscard]] inline double nextGaussianApprox() noexcept
    {
        // Sum of 3 xorshift32 uniforms -> smooth bell-shaped thermal noise in [-1, +1]
        double sum = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            state_ ^= state_ << 13;
            state_ ^= state_ >> 17;
            state_ ^= state_ << 5;
            sum += (static_cast<double> (state_) / 2147483648.0) - 1.0;
        }
        return sum * 0.3333333333333333;
    }

private:
    std::uint32_t state_;
};

/**
 * Complete Single-Channel Neve 1073 / 1290 Discrete Class-A Preamp Circuit Model:
 *   Input -> [DAW Pad if Auto-Gain] -> Marinair T10468 Input Transformer (Hi-Z / Lo-Z)
 *         -> PL20033 Rotary Deck -> [BA284 Preamp 1 if >= 55 dB] -> BA283NV Preamp 2
 *         -> Channel Fader -> BA283AM (2N3055 Class-A Driver) -> Marinair LO1166 Gapped Output Iron
 *         -> Output Trim / Phase Invert
 */
class Neve1073Channel
{
public:
    explicit Neve1073Channel (std::uint32_t noiseSeed = 0x1073BA28u) noexcept
        : inputTransformer_ (TransformerModel::Type::MarinairT10468Input),
          ba284Stage1_ (BA283Stage::Role::PreampNV),
          ba283nvStage2_ (BA283Stage::Role::PreampNV),
          ba283amOutput_ (BA283Stage::Role::OutputAM),
          noiseGen_ (noiseSeed)
    {
    }

    void prepare (double baseSampleRate) noexcept
    {
        baseSampleRate_ = std::max (22050.0, baseSampleRate);
        inScaleSmoother_.prepare (baseSampleRate_, 10.0);
        faderSmoother_.prepare (baseSampleRate_, 10.0);
        outScaleSmoother_.prepare (baseSampleRate_, 10.0);

        updateOversampledRate (currentOversampling_);
        reset();
    }

    void reset() noexcept
    {
        oversampler_.reset();
        inputTransformer_.reset();
        ba284Stage1_.reset();
        ba283nvStage2_.reset();
        ba283amOutput_.reset();
        outputTransformer_.reset();
    }

    void updateParameters (const PreampParams& p, bool snapImmediately = false) noexcept
    {
        if (p.oversampling != currentOversampling_)
        {
            currentOversampling_ = p.oversampling;
            updateOversampledRate (currentOversampling_);
        }

        if (p.loZImpedance != currentLoZ_)
        {
            currentLoZ_ = p.loZImpedance;
            inputTransformer_.configureForType (TransformerModel::Type::MarinairT10468Input, currentLoZ_);
        }

        const auto& step = GainSwitchPL20033::getStep (p.gainStepIndex);
        isMicMode_          = step.isMicMode;
        threeStageTopology_ = step.threeStageTopology;
        analogNoiseEnabled_ = p.analogNoise;
        phaseSign_          = p.phaseInvert ? -1.0 : 1.0;

        // Lo-Z (300 ohm parallel primary) adds ~2.5 dB transformer step-up in Mic mode
        // At >= 55 dB (3-stage mode), input impedance drops slightly (~1200 -> 910 ohm), subtly loading source (-0.25 dB)
        const double loZBoostDb     = (step.isMicMode && p.loZImpedance) ? 2.5 : 0.0;
        const double threeStageLoad = (step.threeStageTopology) ? -0.25 : 0.0;

        // When Auto-Gain is ON in Mic mode, pad DAW line-level input (-26 dB) to realistic mic sensitivity
        // so that 20-35 dB = clean/warm console sweet spot, 40-50 dB = edge-of-breakup, 55-80 dB = 3-stage drive!
        const double dawMicPadDb = (p.autoGainComp && step.isMicMode) ? -26.0 : 0.0;

        const double preTransformerGainDb = dawMicPadDb + step.inputStageGainDb + loZBoostDb + threeStageLoad;
        const double inScaleTarget = dbToLinear (preTransformerGainDb);

        // Configure discrete BJT card closed-loop gains & feedback factors
        ba284Stage1_.setStageClosedLoopGainDb (step.ba284Stage1GainDb, true);
        ba283nvStage2_.setStageClosedLoopGainDb (step.ba283nvStage2GainDb, step.isMicMode);
        ba283amOutput_.setStageClosedLoopGainDb (step.ba283amOutputGainDb, true);

        ba284Stage1_.setBiasTrim (p.biasTrim * 0.4);
        ba283nvStage2_.setBiasTrim (p.biasTrim * 0.6);
        ba283amOutput_.setBiasTrim (p.biasTrim);

        // Interstage channel fader sits right between BA283NV and BA283AM (2N3055)
        const double faderLinearTarget = dbToLinear (p.faderDriveDb);

        // Post-LO1166 output trim + optional Auto-Gain makeup attenuation
        double autoMakeupDb = 0.0;
        if (p.autoGainComp && step.isMicMode)
        {
            // Net gain added by circuit with -26 dB DAW pad is (totalNominalGainDb - 26 + loZBoostDb).
            // Compensate post-LO1166 so perceived loudness stays balanced as saturation increases!
            const double netBoostDb = (step.totalNominalGainDb - 26.0) + loZBoostDb + p.faderDriveDb;
            autoMakeupDb = -0.85 * netBoostDb;
        }

        // Normalize internal +26 dBu clip ceiling (1.0) <-> DAW +4 dBFS (+1.585 float) or keep unity small-signal gain!
        // Notice: input * inScale * ba284 * ba283nv * fader * ba283am = exact step.totalNominalGainDb!
        // Scaling internal circuit headroom by headroomNorm_ (0.63 = -4 dBFS -> 1.0 clip) places +26 dBu clip at +4 dBFS!
        const double outScaleTarget = dbToLinear (p.outputTrimDb + autoMakeupDb) / kCircuitHeadroomScale;
        const double calibratedInTarget = inScaleTarget * kCircuitHeadroomScale;

        if (snapImmediately)
        {
            inScaleSmoother_.reset (calibratedInTarget);
            faderSmoother_.reset (faderLinearTarget);
            outScaleSmoother_.reset (outScaleTarget);
        }
        else
        {
            inScaleSmoother_.setTarget (calibratedInTarget);
            faderSmoother_.setTarget (faderLinearTarget);
            outScaleSmoother_.setTarget (outScaleTarget);
        }
    }

    [[nodiscard]] inline float processSample (float inputSample, double& satMetricOut) noexcept
    {
        double strayOut = 0.0;
        return processSampleWithBus (inputSample, 1.0, 0.0, 0.0, satMetricOut, strayOut);
    }

    [[nodiscard]] inline float processSampleWithBus (float inputSample,
                                                     double railVoltageScale,
                                                     double psuRippleVolts,
                                                     double inducedCrosstalk,
                                                     double& satMetricOut,
                                                     double& strayFieldOut) noexcept
    {
        ba284Stage1_.setSupplyRailScale (railVoltageScale);
        ba283nvStage2_.setSupplyRailScale (railVoltageScale);
        ba283amOutput_.setSupplyRailScale (railVoltageScale);

        const double inGain   = inScaleSmoother_.getNext();
        const double faderLin = faderSmoother_.getNext();
        const double outGain  = outScaleSmoother_.getNext();

        // Equivalent Input Noise + adjacent-slot T10468 magnetic pickup + PSRR-attenuated PSU ripple
        const double einNoise = analogNoiseEnabled_ ? (noiseGen_.nextGaussianApprox() * 4.5e-7) : 0.0;
        const double xPreIron = static_cast<double> (inputSample) * inGain
                              + einNoise
                              + inducedCrosstalk * 0.25;

        double satAccum = 0.0;
        double lastStray = 0.0;

        const double yPostIron = oversampler_.processSample (
            xPreIron,
            currentOversampling_,
            [this, faderLin, psuRippleVolts, &satAccum, &lastStray] (double s) noexcept -> double
            {
                // 1. Marinair T10468 Input Transformer
                double v = inputTransformer_.processSample (s);

                // 2. PL20033 Gain Switch Routing:
                if (isMicMode_)
                {
                    if (threeStageTopology_)
                        v = ba284Stage1_.processSample (v);

                    v = ba283nvStage2_.processSample (v);
                }

                // 3. Interstage Channel Fader + subtle single-ended Class-A PSRR supply ripple feedthrough
                v = v * faderLin + psuRippleVolts * 0.015;
                v = ba283amOutput_.processSample (v);

                // 4. Marinair LO1166 Gapped Class-A Output Transformer
                v = outputTransformer_.processSample (v);

                satAccum = std::max (satAccum,
                                     ba283nvStage2_.getSaturationAmount()
                                     + ba283amOutput_.getSaturationAmount()
                                     + 1.8 * (inputTransformer_.getCoreStress() + outputTransformer_.getCoreStress()));
                lastStray = outputTransformer_.getStrayField() + 0.35 * inputTransformer_.getStrayField();
                return v;
            });

        satMetricOut  = satAccum;
        strayFieldOut = lastStray;
        const double finalOut = yPostIron * outGain * phaseSign_;

        if (! std::isfinite (finalOut))
            return 0.0f;

        return static_cast<float> (finalOut);
    }

    [[nodiscard]] double getEffectiveInputImpedanceOhms() const noexcept
    {
        if (! isMicMode_)
            return 10000.0; // Line bridging input
        if (currentLoZ_)
            return threeStageTopology_ ? 260.0 : 300.0;
        return threeStageTopology_ ? 910.0 : 1200.0;
    }

private:
    // Maps DAW 0 dBFS (1.0) to +22 dBu so +26 dBu hardware rail clip sits cleanly at +4 dBFS (1.585)
    static constexpr double kCircuitHeadroomScale = 0.63095734448; // 10^(-4/20)

    [[nodiscard]] static inline double dbToLinear (double db) noexcept
    {
        return std::pow (10.0, db / 20.0);
    }

    void updateOversampledRate (Oversampler::Factor factor) noexcept
    {
        const double mult = static_cast<double> (static_cast<int> (factor));
        const double osRate = baseSampleRate_ * mult;

        inputTransformer_.configureForType (TransformerModel::Type::MarinairT10468Input, currentLoZ_);
        outputTransformer_.configureForType (TransformerModel::Type::MarinairLO1166Output, false);

        inputTransformer_.prepare (osRate);
        ba284Stage1_.prepare (osRate);
        ba283nvStage2_.prepare (osRate);
        ba283amOutput_.prepare (osRate);
        outputTransformer_.prepare (osRate);
    }

    double baseSampleRate_ { 48000.0 };
    Oversampler::Factor currentOversampling_ { Oversampler::Factor::x4 };
    bool currentLoZ_         { false };
    bool isMicMode_          { true };
    bool threeStageTopology_ { false };
    bool analogNoiseEnabled_ { false };
    double phaseSign_        { 1.0 };

    Oversampler      oversampler_;
    TransformerModel inputTransformer_;
    BA283Stage       ba284Stage1_;
    BA283Stage       ba283nvStage2_;
    BA283Stage       ba283amOutput_;
    TransformerModel outputTransformer_ { TransformerModel::Type::MarinairLO1166Output };
    WhiteNoiseGen    noiseGen_;

    OnePoleSmoother inScaleSmoother_;
    OnePoleSmoother faderSmoother_;
    OnePoleSmoother outScaleSmoother_;
};

/**
 * Multi-channel (Mono/Stereo) Neve 1073 Preamp Processor with thread-safe metering atomics for UI.
 */
class Neve1073PreampDSP
{
public:
    static constexpr std::size_t kMaxChannels = 2;

    Neve1073PreampDSP() noexcept
        : channels_ {{ Neve1073Channel (0x1073AAA1u), Neve1073Channel (0x1073BBB2u) }}
    {
    }

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        meterBallisticCoeff_ = 1.0 - std::exp (-1.0 / (0.045 * sampleRate)); // Fast peak/VU hybrid
        for (auto& ch : channels_)
            ch.prepare (sampleRate);
    }

    void reset() noexcept
    {
        for (auto& ch : channels_)
            ch.reset();
        inEnv_  = 0.0;
        outEnv_ = 0.0;
        satEnv_ = 0.0;
    }

    void setParameters (const PreampParams& params, bool snap = false) noexcept
    {
        activeStepIndex_.store (params.gainStepIndex, std::memory_order_relaxed);
        threeStageActive_.store (GainSwitchPL20033::getStep (params.gainStepIndex).threeStageTopology,
                                 std::memory_order_relaxed);
        for (auto& ch : channels_)
            ch.updateParameters (params, snap);
    }

    void processBlock (float* const* channelPointers, int numChannels, int numSamples) noexcept
    {
        const int chCount = std::min (numChannels, static_cast<int> (kMaxChannels));
        if (chCount <= 0 || numSamples <= 0)
            return;

        double blockPeakIn  = 0.0;
        double blockPeakOut = 0.0;
        double blockPeakSat = 0.0;

        for (int ch = 0; ch < chCount; ++ch)
        {
            float* buf = channelPointers[ch];
            if (buf == nullptr)
                continue;

            auto& proc = channels_[static_cast<std::size_t> (ch)];
            for (int i = 0; i < numSamples; ++i)
            {
                const float inSample = buf[i];
                blockPeakIn = std::max (blockPeakIn, static_cast<double> (std::abs (inSample)));

                double sat = 0.0;
                const float outSample = proc.processSample (inSample, sat);
                buf[i] = outSample;

                blockPeakOut = std::max (blockPeakOut, static_cast<double> (std::abs (outSample)));
                blockPeakSat = std::max (blockPeakSat, sat);
            }
        }

        // Copy channel 0 to extra channels beyond 2 if ever hosted in multichannel bus
        for (int ch = chCount; ch < numChannels; ++ch)
        {
            if (channelPointers[ch] != nullptr && channelPointers[0] != nullptr)
                std::copy_n (channelPointers[0], numSamples, channelPointers[ch]);
        }

        const double blockRelease = std::pow (1.0 - meterBallisticCoeff_, static_cast<double> (numSamples));
        inEnv_  = std::max (blockPeakIn,  inEnv_  * blockRelease);
        outEnv_ = std::max (blockPeakOut, outEnv_ * blockRelease);
        satEnv_ = std::max (blockPeakSat, satEnv_ * blockRelease);

        inputMeterLinear_.store (static_cast<float> (inEnv_), std::memory_order_relaxed);
        outputMeterLinear_.store (static_cast<float> (outEnv_), std::memory_order_relaxed);
        saturationMeter_.store (static_cast<float> (std::clamp (satEnv_ * 2.8, 0.0, 1.0)), std::memory_order_relaxed);
    }

    /**
     * Single-frame entry point used when mounted inside a 500-series Lunchbox slot.
     */
    inline void processFrameBackplane (float& left,
                                       float& right,
                                       int numChannels,
                                       double railVoltageScale,
                                       double psuRippleVolts,
                                       double inducedCrosstalk,
                                       double& strayFieldOut,
                                       double& dynamicCurrentMaOut,
                                       double& inputZOhmsOut) noexcept
    {
        const double peakIn = (numChannels > 1)
            ? std::max (std::abs (static_cast<double> (left)), std::abs (static_cast<double> (right)))
            : std::abs (static_cast<double> (left));

        double satL = 0.0, strayL = 0.0;
        left = channels_[0].processSampleWithBus (left, railVoltageScale, psuRippleVolts, inducedCrosstalk, satL, strayL);

        double satR = 0.0, strayR = 0.0;
        if (numChannels > 1)
            right = channels_[1].processSampleWithBus (right, railVoltageScale, psuRippleVolts, inducedCrosstalk, satR, strayR);
        else
            right = left;

        const double peakOut = (numChannels > 1)
            ? std::max (std::abs (static_cast<double> (left)), std::abs (static_cast<double> (right)))
            : std::abs (static_cast<double> (left));
        const double peakSat = std::max (satL, satR);

        const double oneSampleRel = 1.0 - meterBallisticCoeff_;
        inEnv_  = std::max (peakIn,  inEnv_  * oneSampleRel);
        outEnv_ = std::max (peakOut, outEnv_ * oneSampleRel);
        satEnv_ = std::max (peakSat, satEnv_ * oneSampleRel);

        if ((++meterSubsampleCounter_ & 63) == 0)
        {
            inputMeterLinear_.store (static_cast<float> (inEnv_), std::memory_order_relaxed);
            outputMeterLinear_.store (static_cast<float> (outEnv_), std::memory_order_relaxed);
            saturationMeter_.store (static_cast<float> (std::clamp (satEnv_ * 2.8, 0.0, 1.0)), std::memory_order_relaxed);
        }

        strayFieldOut       = (numChannels > 1) ? 0.5 * (strayL + strayR) : strayL;
        // 2N3055 Class-A dynamic rail demand rises up to +38 mA under heavy transformer/saturation drive,
        // plus +12 mA when the auxiliary BA284 3-stage card is powered in >= 55 dB mode!
        dynamicCurrentMaOut = (isThreeStageActive() ? 12.0 : 0.0)
                            + std::clamp (peakOut * 14.0 + peakSat * 28.0, 0.0, 42.0);
        inputZOhmsOut       = channels_[0].getEffectiveInputImpedanceOhms();
    }

    [[nodiscard]] float getInputMeterLinear() const noexcept  { return inputMeterLinear_.load (std::memory_order_relaxed); }
    [[nodiscard]] float getOutputMeterLinear() const noexcept { return outputMeterLinear_.load (std::memory_order_relaxed); }
    [[nodiscard]] float getSaturationMeter() const noexcept   { return saturationMeter_.load (std::memory_order_relaxed); }
    [[nodiscard]] bool  isThreeStageActive() const noexcept   { return threeStageActive_.load (std::memory_order_relaxed); }
    [[nodiscard]] int   getActiveStepIndex() const noexcept   { return activeStepIndex_.load (std::memory_order_relaxed); }

private:
    std::array<Neve1073Channel, kMaxChannels> channels_;
    double sampleRate_ { 48000.0 };
    double meterBallisticCoeff_ { 0.001 };

    double inEnv_  { 0.0 };
    double outEnv_ { 0.0 };
    double satEnv_ { 0.0 };
    std::uint32_t meterSubsampleCounter_ { 0 };

    std::atomic<float> inputMeterLinear_  { 0.0f };
    std::atomic<float> outputMeterLinear_ { 0.0f };
    std::atomic<float> saturationMeter_   { 0.0f };
    std::atomic<bool>  threeStageActive_  { false };
    std::atomic<int>   activeStepIndex_   { 3 };
};

} // namespace nv73::dsp
