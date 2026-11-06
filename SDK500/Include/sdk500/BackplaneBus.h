#pragma once

namespace sdk500
{

/**
 * VPR Alliance 500-Series 15-Pin Card-Edge Electrical Context supplied BY the Chassis Backplane
 * TO each module slot on every sample:
 *   - Pin 12 (+16V DC) / Pin 14 (-16V DC) regulated or sagging rail voltage scale
 *   - Pin 13 (0V Power Ground) 120 Hz linear PSU ripple under overload
 *   - Adjacent-slot stray transformer magnetic field coupling
 */
struct SlotPowerContext
{
    double railVoltageScale       { 1.0 }; // 1.0 = nominal +/-16.0V; < 1.0 when shared linear PSU droops
    double psuRippleVolts         { 0.0 }; // Normalized supply ripple (attenuated by module PSRR)
    double inducedCrosstalkSample { 0.0 }; // Magnetic pickup from slots (s - 1) and (s + 1)
};

/**
 * Real-time electrical telemetry reported BY each 500-Series module BACK to the Chassis Backplane:
 *   - Pin 12/14 quiescent + signal-dependent Class-A/AB current draw (VPR spec = 130 mA/slot)
 *   - Pin 8/10 balanced input impedance (e.g. 1200 ohm Hi-Z, 300 ohm Lo-Z, 10k bridging)
 *   - Pin 2/4  balanced output impedance (e.g. 75 ohm LO1166 secondary, 600 ohm passive pad)
 *   - Instantaneous stray magnetic field radiated by onboard input/output transformers
 */
struct SlotTelemetry
{
    double idleCurrentMa       { 0.0 };
    double dynamicCurrentMa    { 0.0 };
    double inputImpedanceOhms  { 10000.0 };
    double outputImpedanceOhms { 75.0 };
    double strayMagneticFlux   { 0.0 };
};

} // namespace sdk500
