#pragma once

#include <array>
#include <string_view>

namespace nv73::dsp
{

/**
 * Models the Neve PL20033 Multi-Deck Rotary Sensitivity Switch ("The Red Knob").
 *
 * 14 Detented Positions:
 *   Index 0      : LINE (0 dB unity line-input path through T10468/LO1166 + BA283AM)
 *   Index 1..7   : 20, 25, 30, 35, 40, 45, 50 dB  -> 2-Stage Topology (BA283NV + BA283AM)
 *   Index 8..13  : 55, 60, 65, 70, 75, 80 dB      -> 3-Stage Topology (BA284 + BA283NV + BA283AM)
 *
 * Notice the famous hardware transition between Index 7 (50 dB) and Index 8 (55 dB):
 *   - At 50 dB: Single BA283NV preamp stage runs at 38 dB closed-loop gain (minimal NFB, chewy 2nd harmonic).
 *   - At 55 dB: PL20033 switches in the auxiliary BA284 3-transistor preamp stage (23 dB) AND resets
 *               BA283NV back down to 20 dB closed-loop gain, shifting bandwidth, EIN, and transient contour.
 */
struct GainStepSpec
{
    std::string_view label;
    double totalNominalGainDb; // Total small-signal gain in Hi-Z (1200 ohm) mode
    bool   isMicMode;          // False for LINE (0 dB), True for 20..80 dB
    bool   threeStageTopology; // True for 55..80 dB (engages BA284 Stage 1 ahead of BA283NV Stage 2)
    double inputStageGainDb;   // T10468 transformer + input deck net gain/pad (dB)
    double ba284Stage1GainDb;  // BA284 Preamp 1 closed-loop gain (0 dB when bypassed)
    double ba283nvStage2GainDb;// BA283NV Preamp 2 closed-loop gain (dB)
    double ba283amOutputGainDb;// BA283AM 2N3055 Output stage makeup gain (dB)
};

class GainSwitchPL20033
{
public:
    static constexpr int kNumSteps = 14;

    static constexpr std::array<GainStepSpec, kNumSteps> kSteps {{
        // Label,   Tot,  Mic,   3Stg,  InDeck, BA284, BA283NV, BA283AM
        { "LINE",   0.0,  false, false, -12.0,   0.0,    0.0,    12.0 }, // 0: Line unity through iron + BA283AM
        { "20",    20.0,  true,  false,   0.0,   0.0,    8.0,    12.0 }, // 1: 2-stage min mic gain
        { "25",    25.0,  true,  false,   0.0,   0.0,   13.0,    12.0 }, // 2
        { "30",    30.0,  true,  false,   0.0,   0.0,   18.0,    12.0 }, // 3
        { "35",    35.0,  true,  false,   0.0,   0.0,   23.0,    12.0 }, // 4
        { "40",    40.0,  true,  false,   0.0,   0.0,   28.0,    12.0 }, // 5
        { "45",    45.0,  true,  false,   0.0,   0.0,   33.0,    12.0 }, // 6
        { "50",    50.0,  true,  false,   0.0,   0.0,   38.0,    12.0 }, // 7: Max 2-stage push!
        // --- 50 dB -> 55 dB Topology Switch (PL20033 engages BA284 card!) ---
        { "55",    55.0,  true,  true,    0.0,  23.0,   20.0,    12.0 }, // 8: Fresh 3-stage cascade
        { "60",    60.0,  true,  true,    0.0,  26.0,   22.0,    12.0 }, // 9
        { "65",    65.0,  true,  true,    0.0,  28.0,   25.0,    12.0 }, // 10
        { "70",    70.0,  true,  true,    0.0,  30.0,   28.0,    12.0 }, // 11
        { "75",    75.0,  true,  true,    0.0,  32.0,   31.0,    12.0 }, // 12
        { "80",    80.0,  true,  true,    0.0,  34.0,   34.0,    12.0 }  // 13: Full 80 dB 3-stage cascade!
    }};

    [[nodiscard]] static constexpr const GainStepSpec& getStep (int index) noexcept
    {
        const int clamped = (index < 0) ? 0 : ((index >= kNumSteps) ? (kNumSteps - 1) : index);
        return kSteps[static_cast<std::size_t> (clamped)];
    }
};

} // namespace nv73::dsp
