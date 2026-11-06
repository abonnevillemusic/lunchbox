#pragma once

#include <sdk500/Module500Interface.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>

namespace lbx500::dsp
{

enum class PsuTopology
{
    VintageLinearShared, // Shared 780 mA (6 x 130 mA) linear transformer/rectifier PSU with dynamic rail sag & 120 Hz ripple
    IsolatedOSTRegulated // Per-slot isolated high-current linear regulators (rock-steady +/-16.00V, zero cross-slot sag)
};

struct ChassisMasterParams
{
    double      inputTrimDb      { 0.0 };
    double      outputTrimDb     { 0.0 };
    double      crosstalkScale   { 1.0 }; // 0.0 = off, 1.0 = authentic VPR backplane, 2.0 = heavy vintage bleed
    PsuTopology psuTopology      { PsuTopology::VintageLinearShared };
    bool        terminateOut600  { false }; // Master output 600-ohm load vs 10k modern converter bridging
};

struct SlotRuntimeEntry
{
    std::shared_ptr<sdk500::Module500> module;
    bool bypassed { false };
    bool soloed   { false };
};

/**
 * 6-Slot VPR Alliance 500-Series Chassis & +/-16V Linear Power Supply Backplane Engine.
 */
class ChassisBackplaneDSP
{
public:
    static constexpr int    kNumSlots            = 6;
    static constexpr double kNominalRailVolts    = 16.0;
    static constexpr double kRatedCurrentMaTotal = 780.0; // 6 slots * 130 mA VPR spec

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::max (22050.0, sampleRate);

        // Linear PSU 4700 uF reservoir capacitor sag attack (~3.5 ms) & recharge release (~72 ms)
        sagAttackCoeff_  = 1.0 - std::exp (-1.0 / (0.0035 * sampleRate_));
        sagReleaseCoeff_ = 1.0 - std::exp (-1.0 / (0.0720 * sampleRate_));

        // Parameter smoothers (~10 ms)
        inTrimSmoothCoeff_ = 1.0 - std::exp (-1.0 / (0.010 * sampleRate_));
        meterReleaseCoeff_ = 1.0 - std::exp (-1.0 / (0.050 * sampleRate_));

        // 55 Hz high-pass on inductive magnetic crosstalk (V_induced = M * dPhi/dt)
        xtalkHpR_ = std::exp (-(2.0 * std::numbers::pi * 55.0) / sampleRate_);

        ripplePhaseInc_ = (2.0 * std::numbers::pi * 120.0) / sampleRate_; // 120 Hz full-wave bridge ripple

        reset();
    }

    void reset() noexcept
    {
        psuSagVoltsEnv_ = 0.0;
        ripplePhase_    = 0.0;
        prevStrayFlux_.fill (0.0);
        xtalkXPrev_.fill (0.0);
        xtalkYPrev_.fill (0.0);
        inEnv_  = 0.0;
        outEnv_ = 0.0;
    }

    void setMasterParams (const ChassisMasterParams& p, bool snap = false) noexcept
    {
        inGainTarget_    = std::pow (10.0, p.inputTrimDb / 20.0);
        outGainTarget_   = std::pow (10.0, p.outputTrimDb / 20.0);
        crosstalkScale_  = std::clamp (p.crosstalkScale, 0.0, 2.5);
        psuTopology_     = p.psuTopology;
        terminateOut600_ = p.terminateOut600;

        if (snap)
        {
            inGainCurrent_  = inGainTarget_;
            outGainCurrent_ = outGainTarget_;
        }
    }

    void processBlock (float* const* channelPointers,
                       int numChannels,
                       int numSamples,
                       const std::array<SlotRuntimeEntry, kNumSlots>& slots) noexcept
    {
        if (numChannels <= 0 || numSamples <= 0 || channelPointers[0] == nullptr)
            return;

        float* leftBuf  = channelPointers[0];
        float* rightBuf = (numChannels > 1 && channelPointers[1] != nullptr) ? channelPointers[1] : nullptr;

        // Check if any occupied slot has SOLO engaged
        bool anySolo = false;
        for (const auto& s : slots)
            if (s.module != nullptr && s.soloed)
                anySolo = true;

        // Determine active audio participation per slot
        std::array<bool, kNumSlots> slotAudioActive {};
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (slots[static_cast<std::size_t> (i)].module == nullptr)
                slotAudioActive[static_cast<std::size_t> (i)] = false;
            else if (anySolo)
                slotAudioActive[static_cast<std::size_t> (i)] = slots[static_cast<std::size_t> (i)].soloed
                                                              && ! slots[static_cast<std::size_t> (i)].bypassed;
            else
                slotAudioActive[static_cast<std::size_t> (i)] = ! slots[static_cast<std::size_t> (i)].bypassed;
        }

        double blockPeakIn   = 0.0;
        double blockPeakOut  = 0.0;
        double lastTotalMa   = 0.0;
        double lastRailVolts = kNominalRailVolts;

        // Cache last known input impedances for inter-slot Pin 2/4 -> Pin 8/10 voltage divider calculation
        for (int i = 0; i < numSamples; ++i)
        {
            inGainCurrent_  += inTrimSmoothCoeff_ * (inGainTarget_  - inGainCurrent_);
            outGainCurrent_ += inTrimSmoothCoeff_ * (outGainTarget_ - outGainCurrent_);

            float l = leftBuf[i] * static_cast<float> (inGainCurrent_);
            float r = (rightBuf != nullptr) ? (rightBuf[i] * static_cast<float> (inGainCurrent_)) : l;

            const double inPeak = std::max (std::abs (static_cast<double> (l)), std::abs (static_cast<double> (r)));
            blockPeakIn = std::max (blockPeakIn, inPeak);

            // 1. Compute instantaneous +/-16V rail scale & 120 Hz rectifier ripple from current PSU state
            double railScale = 1.0;
            double psuRipple = 0.0;

            if (psuTopology_ == PsuTopology::VintageLinearShared)
            {
                const double railV = std::clamp (kNominalRailVolts - psuSagVoltsEnv_, 11.8, kNominalRailVolts);
                railScale = railV / kNominalRailVolts;
                lastRailVolts = railV;

                ripplePhase_ += ripplePhaseInc_;
                if (ripplePhase_ >= 2.0 * std::numbers::pi)
                    ripplePhase_ -= 2.0 * std::numbers::pi;

                // Full-wave rectified 120 Hz ripple rises smoothly only when PSU load stresses the regulator
                const double overloadRatio = std::max (0.0, psuSagVoltsEnv_ - 0.35);
                psuRipple = overloadRatio * 0.0025 * std::sin (ripplePhase_);
            }
            else
            {
                lastRailVolts = kNominalRailVolts;
            }

            // 2. Traverse Slots 1..6 sequentially across the 500-series backplane
            double sampleTotalMa = 0.0;
            std::array<double, kNumSlots> currentStrayFlux {};

            double prevActiveZOut = 0.0;
            bool   hasPrevActive  = false;

            for (int s = 0; s < kNumSlots; ++s)
            {
                const auto idx = static_cast<std::size_t> (s);
                const auto& entry = slots[idx];

                if (entry.module == nullptr)
                    continue;

                // Even relay-bypassed modules stay plugged into Pin 12/14 +/-16V power rails!
                if (! slotAudioActive[idx])
                {
                    sampleTotalMa += entry.module->getDescriptor().idleCurrentMa;
                    continue;
                }

                // Compute adjacent-slot transformer magnetic pickup from (s - 1) and (s + 1)
                double rawAdjacentFlux = 0.0;
                if (s > 0)
                    rawAdjacentFlux += prevStrayFlux_[idx - 1];
                if (s + 1 < kNumSlots)
                    rawAdjacentFlux += prevStrayFlux_[idx + 1];

                // Inductive coupling dPhi/dt high-pass filter
                const double xtalkHp = rawAdjacentFlux - xtalkXPrev_[idx] + xtalkHpR_ * xtalkYPrev_[idx];
                xtalkXPrev_[idx] = rawAdjacentFlux;
                xtalkYPrev_[idx] = xtalkHp;

                sdk500::SlotPowerContext ctx;
                ctx.railVoltageScale       = railScale;
                ctx.psuRippleVolts         = psuRipple;
                ctx.inducedCrosstalkSample = xtalkHp * 0.0018 * crosstalkScale_;

                sdk500::SlotTelemetry tel {};

                // Apply inter-slot Pin-2/4 Z_out -> Pin-8/10 Z_in bridging/loading divider BEFORE entering this module!
                // (Uses previous sample's reported inputZ or first-pass estimate, updated immediately below)
                if (hasPrevActive && slotInputZCache_[idx] > 1.0)
                {
                    const float zDivider = static_cast<float> (
                        slotInputZCache_[idx] / (prevActiveZOut + slotInputZCache_[idx]));
                    l *= zDivider;
                    r *= zDivider;
                }

                entry.module->processSampleWithBackplane (l, r, numChannels, ctx, tel);

                slotInputZCache_[idx]  = std::max (50.0, tel.inputImpedanceOhms);
                prevActiveZOut         = std::max (1.0,  tel.outputImpedanceOhms);
                hasPrevActive          = true;

                sampleTotalMa         += (tel.idleCurrentMa + tel.dynamicCurrentMa);
                currentStrayFlux[idx]  = tel.strayMagneticFlux;
            }

            // Final chassis output load divider (600-ohm classic line termination vs 10k modern converter bridging)
            if (hasPrevActive)
            {
                const double chassisLoadZ = terminateOut600_ ? 600.0 : 10000.0;
                const float outZDivider = static_cast<float> (chassisLoadZ / (prevActiveZOut + chassisLoadZ));
                l *= outZDivider;
                r *= outZDivider;
            }

            prevStrayFlux_ = currentStrayFlux;
            lastTotalMa    = sampleTotalMa;

            // 3. Update Linear PSU Reservoir Sag Envelope from total 6-slot current demand
            if (psuTopology_ == PsuTopology::VintageLinearShared)
            {
                // Gentle resistive droop up to 780 mA (~0.75V at 780 mA), steeper regulator foldback above 780 mA!
                const double loadRatio = sampleTotalMa / kRatedCurrentMaTotal;
                double targetDropVolts = 0.72 * loadRatio;
                if (loadRatio > 0.85)
                {
                    const double excess = loadRatio - 0.85;
                    targetDropVolts += 4.2 * excess * excess;
                }
                targetDropVolts = std::clamp (targetDropVolts, 0.0, 3.8);

                const double c = (targetDropVolts > psuSagVoltsEnv_) ? sagAttackCoeff_ : sagReleaseCoeff_;
                psuSagVoltsEnv_ += c * (targetDropVolts - psuSagVoltsEnv_);
            }
            else
            {
                psuSagVoltsEnv_ = 0.0;
            }

            // 4. Master Output Trim & Safety Guard
            l *= static_cast<float> (outGainCurrent_);
            r *= static_cast<float> (outGainCurrent_);
            if (! std::isfinite (l)) l = 0.0f;
            if (! std::isfinite (r)) r = 0.0f;

            leftBuf[i] = l;
            if (rightBuf != nullptr)
                rightBuf[i] = r;

            const double outPeak = std::max (std::abs (static_cast<double> (l)), std::abs (static_cast<double> (r)));
            blockPeakOut = std::max (blockPeakOut, outPeak);
        }

        for (int ch = 2; ch < numChannels; ++ch)
            if (channelPointers[ch] != nullptr)
                std::copy_n (leftBuf, numSamples, channelPointers[ch]);

        // Update lock-free UI telemetry atomics
        const double blockRel = std::pow (1.0 - meterReleaseCoeff_, static_cast<double> (numSamples));
        inEnv_  = std::max (blockPeakIn,  inEnv_  * blockRel);
        outEnv_ = std::max (blockPeakOut, outEnv_ * blockRel);

        totalCurrentMa_.store   (static_cast<float> (lastTotalMa),   std::memory_order_relaxed);
        railVoltageVolts_.store (static_cast<float> (lastRailVolts), std::memory_order_relaxed);
        masterInMeter_.store    (static_cast<float> (inEnv_),        std::memory_order_relaxed);
        masterOutMeter_.store   (static_cast<float> (outEnv_),       std::memory_order_relaxed);
    }

    [[nodiscard]] float getTotalCurrentMa() const noexcept   { return totalCurrentMa_.load (std::memory_order_relaxed); }
    [[nodiscard]] float getRailVoltageVolts() const noexcept { return railVoltageVolts_.load (std::memory_order_relaxed); }
    [[nodiscard]] float getMasterInMeter() const noexcept    { return masterInMeter_.load (std::memory_order_relaxed); }
    [[nodiscard]] float getMasterOutMeter() const noexcept   { return masterOutMeter_.load (std::memory_order_relaxed); }

private:
    double sampleRate_ { 48000.0 };

    double sagAttackCoeff_   { 0.005 };
    double sagReleaseCoeff_  { 0.0003 };
    double inTrimSmoothCoeff_ { 0.002 };
    double meterReleaseCoeff_ { 0.001 };
    double xtalkHpR_         { 0.992 };
    double ripplePhaseInc_   { 0.015 };

    double inGainTarget_   { 1.0 }, inGainCurrent_  { 1.0 };
    double outGainTarget_  { 1.0 }, outGainCurrent_ { 1.0 };
    double crosstalkScale_ { 1.0 };
    PsuTopology psuTopology_ { PsuTopology::VintageLinearShared };
    bool terminateOut600_  { false };

    double psuSagVoltsEnv_ { 0.0 };
    double ripplePhase_    { 0.0 };
    std::array<double, kNumSlots> prevStrayFlux_ {};
    std::array<double, kNumSlots> xtalkXPrev_ {};
    std::array<double, kNumSlots> xtalkYPrev_ {};
    std::array<double, kNumSlots> slotInputZCache_ { 1200.0, 1200.0, 1200.0, 1200.0, 1200.0, 1200.0 };

    double inEnv_  { 0.0 };
    double outEnv_ { 0.0 };

    std::atomic<float> totalCurrentMa_   { 0.0f };
    std::atomic<float> railVoltageVolts_ { 16.0f };
    std::atomic<float> masterInMeter_    { 0.0f };
    std::atomic<float> masterOutMeter_   { 0.0f };
};

} // namespace lbx500::dsp
