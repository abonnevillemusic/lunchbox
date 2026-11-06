#pragma once

#include "BackplaneBus.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <memory>

namespace sdk500
{

struct ModuleDescriptor
{
    juce::String id;            // Stable serialization ID (e.g. "nv73lb")
    juce::String name;          // Display name in slot selector (e.g. "NV-73LB Preamp")
    juce::String shortCode;     // Compact badge code (e.g. "NV-73LB")
    juce::String category;      // e.g. "Mic Preamp", "EQ", "Dynamics", "Utility"
    double       idleCurrentMa; // Nominal VPR +/-16V rail draw (130 mA spec max per slot)
};

/**
 * Base class implemented by every 500-series module in the pluginz ecosystem.
 * Allows any plugin we build to run BOTH as its own standalone VST3/AU plugin AND natively
 * inside a vertical 1.5" x 5.25" slot of the LBX-500 Lunchbox with full electrical bus coupling.
 */
class Module500
{
public:
    virtual ~Module500() = default;

    [[nodiscard]] virtual const ModuleDescriptor& getDescriptor() const noexcept = 0;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    /**
     * Process a single stereo/mono sample frame while exchanging real-time electrical
     * power-rail sag, impedance loading, and magnetic crosstalk with the 500-series backplane.
     */
    virtual void processSampleWithBackplane (float& left,
                                             float& right,
                                             int numChannels,
                                             const SlotPowerContext& ctx,
                                             SlotTelemetry& telemetryOut) noexcept = 0;

    /** Creates the vertical 500-series faceplate editor (~144 x 430 px) mounted inside a rack slot. */
    [[nodiscard]] virtual std::unique_ptr<juce::Component> createSlotEditor() = 0;

    /** Serialize / restore module state inside Chassis DAW session or rack presets. */
    [[nodiscard]] virtual std::unique_ptr<juce::XmlElement> saveStateXml() const = 0;
    virtual void loadStateXml (const juce::XmlElement& xml) = 0;
};

} // namespace sdk500
