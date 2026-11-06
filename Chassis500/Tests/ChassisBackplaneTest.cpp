#include "../Source/DSP/ChassisBackplaneDSP.h"
#include <sdk500/ModuleRegistry.h>

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

int main()
{
    using namespace lbx500::dsp;

    constexpr double kSampleRate = 48000.0;
    ChassisBackplaneDSP backplane;
    backplane.prepare (kSampleRate);

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

    auto runSineBlock = [&] (std::array<SlotRuntimeEntry, 6>& slots, double freqHz, float amp, int numSamples = 8192) -> float
    {
        std::vector<float> bufL (static_cast<std::size_t> (numSamples));
        std::vector<float> bufR (static_cast<std::size_t> (numSamples));
        for (int n = 0; n < numSamples; ++n)
        {
            const float s = amp * static_cast<float> (std::sin (2.0 * std::numbers::pi * freqHz * n / kSampleRate));
            bufL[static_cast<std::size_t> (n)] = s;
            bufR[static_cast<std::size_t> (n)] = s;
        }
        float* chans[2] = { bufL.data(), bufR.data() };
        backplane.processBlock (chans, 2, numSamples, slots);

        float peak = 0.0f;
        for (int n = numSamples / 2; n < numSamples; ++n)
            peak = std::max (peak, std::abs (bufL[static_cast<std::size_t> (n)]));
        return peak;
    };

    std::cout << "=== 1. Empty 6-Slot Lunchbox Unity Pass-Through & 0 mA Draw ===\n";
    {
        std::array<SlotRuntimeEntry, 6> emptySlots {};
        ChassisMasterParams mp;
        backplane.setMasterParams (mp, true);
        const float outPeak = runSineBlock (emptySlots, 1000.0, 0.25f, 2048);

        check (std::abs (outPeak - 0.25f) < 1e-5f, "Empty 6-slot chassis passes exact bit-accurate unity audio");
        check (backplane.getTotalCurrentMa() == 0.0f, "Empty 6-slot chassis draws 0 mA");
        check (std::abs (backplane.getRailVoltageVolts() - 16.0f) < 0.01f, "Unloaded linear PSU holds +/-16.00V");
    }

    std::cout << "\n=== 2. Active NV-73LB (110+ mA) vs Passive VP-600 (0 mA) & PSU Rail Sag vs Isolated OST ===\n";
    {
        std::array<SlotRuntimeEntry, 6> fullRack {};
        for (int i = 0; i < 6; ++i)
        {
            auto mod = std::shared_ptr<sdk500::Module500> (sdk500::ModuleRegistry::createById ("nv73lb"));
            mod->prepare (kSampleRate, 512);
            // Set to 55 dB 3-stage mode for heavy current demand!
            if (auto* nv = dynamic_cast<nv73::NV73Module500*> (mod.get()))
            {
                nv->getState().gainStepIndex.store (8);
                nv->syncDspParams (true);
            }
            fullRack[static_cast<std::size_t> (i)].module = mod;
        }

        // Vintage Shared Linear PSU under 6x 3-stage Class-A NV-73LB load
        ChassisMasterParams linearMp;
        linearMp.psuTopology = PsuTopology::VintageLinearShared;
        backplane.reset();
        backplane.setMasterParams (linearMp, true);
        (void) runSineBlock (fullRack, 60.0, 0.35f, 12000);

        const float linearMa    = backplane.getTotalCurrentMa();
        const float linearVolts = backplane.getRailVoltageVolts();
        std::cout << "  6x NV-73LB [Vintage Linear PSU]: Load = " << linearMa
                  << " mA, Rail = +/-" << std::fixed << std::setprecision (2) << linearVolts << " V\n";
        check (linearMa > 720.0f, "6x 3-stage Class-A NV-73LBs exceed 720 mA on the 780 mA Lunchbox bus");
        check (linearVolts < 15.35f, "Vintage shared linear PSU sags realistically under heavy 6-slot Class-A load");

        // Switch to Isolated OST Regulated PSU -> rock-solid +/-16.00 V!
        ChassisMasterParams ostMp;
        ostMp.psuTopology = PsuTopology::IsolatedOSTRegulated;
        backplane.setMasterParams (ostMp, true);
        (void) runSineBlock (fullRack, 60.0, 0.35f, 4096);
        const float ostVolts = backplane.getRailVoltageVolts();
        std::cout << "  6x NV-73LB [Isolated OST PSU]  : Load = " << backplane.getTotalCurrentMa()
                  << " mA, Rail = +/-" << ostVolts << " V\n";
        check (std::abs (ostVolts - 16.0f) < 1e-4f, "Isolated OST per-slot regulators hold rock-steady +/-16.00 V");
    }

    std::cout << "\n=== 3. Inter-Slot 600-Ohm LO1166 Transformer Loading & Solo/Bypass Routing ===\n";
    {
        std::array<SlotRuntimeEntry, 6> twoSlot {};
        auto pre  = std::shared_ptr<sdk500::Module500> (sdk500::ModuleRegistry::createById ("nv73lb"));
        auto pad  = std::shared_ptr<sdk500::Module500> (sdk500::ModuleRegistry::createById ("vp600"));
        pre->prepare (kSampleRate, 512);
        pad->prepare (kSampleRate, 512);

        auto* vp = dynamic_cast<sdk500::VP600Module500*> (pad.get());
        vp->attenuationDb().store (0.0f); // 0 dB pad so we isolate pure 600-ohm vs 10k-ohm impedance loading!
        vp->terminate600().store (false); // 10k bridging first

        twoSlot[0].module = pre;
        twoSlot[1].module = pad;

        ChassisMasterParams mp;
        mp.psuTopology = PsuTopology::IsolatedOSTRegulated;
        backplane.reset();
        backplane.setMasterParams (mp, true);
        const float peak10k = runSineBlock (twoSlot, 1000.0, 0.05f, 8192);

        vp->terminate600().store (true); // Engage 600-ohm load across NV-73LB's 75-ohm LO1166 secondary!
        backplane.reset();
        pre->reset();
        const float peak600 = runSineBlock (twoSlot, 1000.0, 0.05f, 8192);

        const double loadLossDb = 20.0 * std::log10 (peak600 / peak10k);
        std::cout << "  600-ohm vs 10k-ohm inter-slot loading delta on LO1166 (75 ohm Z_out): "
                  << loadLossDb << " dB\n";
        // Theoretical: 20*log10(600/675) - 20*log10(10000/10075) = -1.023 - (-0.065) = -0.958 dB!
        check (std::abs (loadLossDb - (-0.958)) < 0.12, "Backplane Z_out -> Z_in divider matches exact -0.96 dB 600-ohm load law");

        // Bypass & Solo verification
        twoSlot[0].bypassed = true;
        vp->attenuationDb().store (-12.0f);
        vp->terminate600().store (false);
        const float bypPeak = runSineBlock (twoSlot, 1000.0, 0.20f, 4096);
        check (bypPeak < 0.06f && bypPeak > 0.04f, "Bypassing Slot 1 routes cleanly through Slot 2 (-12 dB VP-600 pad)");
    }

    std::cout << "\n=== 4. Full 1073 2-Slot Lunchbox Chain: Slot 1 NV-73LB (110 mA) -> Slot 2 NV-73EQ (50 mA) ===\n";
    {
        std::array<SlotRuntimeEntry, 6> channelStrip {};
        auto pre = std::shared_ptr<sdk500::Module500> (sdk500::ModuleRegistry::createById ("nv73lb"));
        auto eq  = std::shared_ptr<sdk500::Module500> (sdk500::ModuleRegistry::createById ("nv73eq"));
        check (pre != nullptr && eq != nullptr, "ModuleRegistry instantiates both nv73lb and nv73eq");

        pre->prepare (kSampleRate, 512);
        eq->prepare  (kSampleRate, 512);

        if (auto* eqMod = dynamic_cast<nv73eq::NV73EQModule500*> (eq.get()))
        {
            eqMod->getState().midFreqStep.store (3);  // 1.6 kHz presence bell
            eqMod->getState().midGainDb.store (9.0f); // +9 dB presence boost
            eqMod->getState().hpfStep.store (2);      // 80 Hz 18 dB/oct inductor HPF
            eqMod->syncDspParams (true);
        }

        channelStrip[0].module = pre;
        channelStrip[1].module = eq;

        ChassisMasterParams mp;
        mp.psuTopology = PsuTopology::VintageLinearShared;
        backplane.reset();
        backplane.setMasterParams (mp, true);

        const float peak40Hz  = runSineBlock (channelStrip, 40.0,   0.05f, 8192);
        backplane.reset();
        pre->reset();
        eq->reset();
        const float peak1600Hz = runSineBlock (channelStrip, 1600.0, 0.05f, 8192);

        const float totalMa = backplane.getTotalCurrentMa();
        std::cout << "  Slot 1 (NV-73LB) -> Slot 2 (NV-73EQ): Total Load = " << totalMa
                  << " mA, @40 Hz peak = " << peak40Hz << ", @1.6 kHz peak = " << peak1600Hz << "\n";
        check (totalMa > 158.0f && totalMa < 210.0f, "2-slot 1073LB + 1073LBEQ pair draws realistic ~165-185 mA on +/-16V bus");
        check (peak1600Hz > peak40Hz * 10.0f, "Chained NV-73LB -> NV-73EQ cuts 40 Hz rumble (80 Hz HPF) and boosts 1.6 kHz presence");
    }

    if (failures > 0)
    {
        std::cerr << "\nFAILED with " << failures << " error(s).\n";
        return EXIT_FAILURE;
    }

    std::cout << "\nALL LBX-500 CHASSIS & ELECTRICAL BACKPLANE CHECKS PASSED!\n";
    return EXIT_SUCCESS;
}
