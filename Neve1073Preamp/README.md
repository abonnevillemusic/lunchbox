# NV-73 Class-A Microphone Preamplifier (Neve 1073 / 1290 Circuit Model)

A C++20 / JUCE audio plugin (**VST3**, **macOS Audio Unit**, and **Standalone**) modeling the discrete Class-A preamplifier path of the classic **Neve 1073** (`EH10023` / `PL20033` / `BA283AV` / `BA284`).

## Modeled Analog Circuit Path

1. **Marinair `T10468` (`TF10003`) Dual-Bobbin Input Transformer (`Source/DSP/TransformerModel.h`)**
   - Implicit Newton-Raphson nonlinear magnetic core flux solver ($\Phi = \int V\,dt$) capturing frequency-dependent bass saturation (`30–150 Hz`), subsonic LC head-bump (~`38–46 Hz`), and Zobel-damped `~20 kHz` secondary resonance.
   - Switchable **`1200Ω Hi-Z`** (series primary) vs. **`300Ω Lo-Z`** (parallel primary, `+2.5 dB` passive step-up and deeper core drive).
2. **`PL20033` 14-Position Stepped Sensitivity Switch (`Source/DSP/GainSwitchPL20033.h`)**
   - **`LINE (0 dB)`**: Unity line-input path through input/output iron and the `BA283AM` `2N3055` Class-A output amplifier.
   - **`20 dB – 50 dB` (2-Stage Mode)**: Routes `T10468` → `BA283NV` (`TR4–TR6` `BC184C` triple) → Channel Fader → `BA283AM`.
   - **`55 dB – 80 dB` (3-Stage Mode)**: Engages the auxiliary **`BA284`** `3x BC184C` preamp stage ahead of `BA283NV` while resetting interstage feedback and subtly shifting input loading (~`910Ω`), faithfully reproducing the famous `50 dB → 55 dB` hardware transition.
3. **`BA283NV` & `BA283AM` (`2N3055`) Discrete Single-Ended Class-A Amplifiers (`Source/DSP/BA283Stage.h`)**
   - Exact-unity-slope single-ended `+24V` rail asymmetric load-line transfer law producing rich **2nd-order even harmonics** ahead of soft clipping.
   - Gain-dependent negative-feedback linearization (`R11 = 33kΩ`) and Miller HF compensation (`C9 = 100pF`, `C11 = 680pF`).
   - Tantalum coupling-capacitor (`C8 = 10µF`, `C5 = 100µF`) dynamic bias sag & recovery envelope, plus front-panel **`RV1` Class-A Bias** symmetry control.
4. **Marinair `LO1166` Gapped Class-A Output Transformer**
   - Models continuous `~60 mA` DC collector current through the gapped steel primary for warm low-frequency 2nd & 3rd harmonic weight.
5. **17th-Order Polyphase Elliptic Oversampling (`Source/DSP/Oversampler.h`)**
   - Selectable `1x`, `2x`, or `4x` zero-allocation Cauer polyphase allpass half-band oversampling (`> 103 dB` alias rejection).

## Building (macOS)

```bash
brew install cmake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j8
ctest --test-dir build --output-on-failure
```

Built bundles are output to:
- **VST3:** `build/Neve1073Preamp_artefacts/Release/VST3/NV-73 Mic Preamp.vst3`
- **AU:** `build/Neve1073Preamp_artefacts/Release/AU/NV-73 Mic Preamp.component`
- **Standalone:** `build/Neve1073Preamp_artefacts/Release/Standalone/NV-73 Mic Preamp.app`
