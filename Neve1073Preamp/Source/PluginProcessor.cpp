#include "PluginProcessor.h"
#include "PluginEditor.h"

Neve1073PreampAudioProcessor::Neve1073PreampAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "NV73_PARAMETERS", createParameterLayout())
{
    gainStepParam_    = apvts_.getRawParameterValue ("gainStep");
    faderDriveParam_  = apvts_.getRawParameterValue ("faderDrive");
    outputTrimParam_  = apvts_.getRawParameterValue ("outputTrim");
    biasTrimParam_    = apvts_.getRawParameterValue ("biasTrim");
    loZParam_         = apvts_.getRawParameterValue ("loZ");
    phaseParam_       = apvts_.getRawParameterValue ("phaseInvert");
    autoGainParam_    = apvts_.getRawParameterValue ("autoGain");
    analogNoiseParam_ = apvts_.getRawParameterValue ("analogNoise");
    oversampleParam_  = apvts_.getRawParameterValue ("oversampling");
}

juce::AudioProcessorValueTreeState::ParameterLayout Neve1073PreampAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    juce::StringArray gainLabels {
        "LINE (0 dB)",
        "20 dB", "25 dB", "30 dB", "35 dB", "40 dB", "45 dB", "50 dB",
        "55 dB", "60 dB", "65 dB", "70 dB", "75 dB", "80 dB"
    };

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "gainStep", 1 },
        "Mic Gain (PL20033)",
        gainLabels,
        3)); // Default: 30 dB

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "faderDrive", 1 },
        "Fader (2N3055 Drive)",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "outputTrim", 1 },
        "Output Trim",
        juce::NormalisableRange<float> (-36.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "biasTrim", 1 },
        "RV1 Class-A Bias",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "loZ", 1 },
        "Lo-Z (300 Ohm)",
        false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "phaseInvert", 1 },
        "Phase Invert",
        false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "autoGain", 1 },
        "Auto-Gain Comp",
        true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "analogNoise", 1 },
        "Analog EIN Hiss",
        false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "oversampling", 1 },
        "Oversampling",
        juce::StringArray { "1x (Off)", "2x", "4x (HQ)" },
        2)); // Default: 4x

    return { params.begin(), params.end() };
}

nv73::dsp::PreampParams Neve1073PreampAudioProcessor::readCurrentParams() const noexcept
{
    nv73::dsp::PreampParams p;
    p.gainStepIndex = static_cast<int> (std::lround (gainStepParam_->load (std::memory_order_relaxed)));
    p.faderDriveDb  = static_cast<double> (faderDriveParam_->load (std::memory_order_relaxed));
    p.outputTrimDb  = static_cast<double> (outputTrimParam_->load (std::memory_order_relaxed));
    p.biasTrim      = static_cast<double> (biasTrimParam_->load (std::memory_order_relaxed)) * 0.01;
    p.loZImpedance  = loZParam_->load (std::memory_order_relaxed) >= 0.5f;
    p.phaseInvert   = phaseParam_->load (std::memory_order_relaxed) >= 0.5f;
    p.autoGainComp  = autoGainParam_->load (std::memory_order_relaxed) >= 0.5f;
    p.analogNoise   = analogNoiseParam_->load (std::memory_order_relaxed) >= 0.5f;

    const int osIdx = static_cast<int> (std::lround (oversampleParam_->load (std::memory_order_relaxed)));
    p.oversampling = (osIdx == 0) ? nv73::dsp::Oversampler::Factor::x1
                   : (osIdx == 1) ? nv73::dsp::Oversampler::Factor::x2
                                  : nv73::dsp::Oversampler::Factor::x4;
    return p;
}

void Neve1073PreampAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    dsp_.prepare (sampleRate);
    dsp_.setParameters (readCurrentParams(), true);
}

void Neve1073PreampAudioProcessor::releaseResources()
{
    dsp_.reset();
}

bool Neve1073PreampAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& outSet = layouts.getMainOutputChannelSet();
    if (outSet != juce::AudioChannelSet::mono() && outSet != juce::AudioChannelSet::stereo())
        return false;

    return outSet == layouts.getMainInputChannelSet();
}

void Neve1073PreampAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    dsp_.setParameters (readCurrentParams(), false);
    dsp_.processBlock (buffer.getArrayOfWritePointers(),
                       buffer.getNumChannels(),
                       buffer.getNumSamples());
}

juce::AudioProcessorEditor* Neve1073PreampAudioProcessor::createEditor()
{
    return new Neve1073PreampAudioProcessorEditor (*this);
}

void Neve1073PreampAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts_.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Neve1073PreampAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts_.state.getType()))
            apvts_.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Neve1073PreampAudioProcessor();
}
