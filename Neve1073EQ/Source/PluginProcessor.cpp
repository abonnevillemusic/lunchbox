#include "PluginProcessor.h"
#include "PluginEditor.h"

Neve1073EQAudioProcessor::Neve1073EQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "NV73EQParameters", createParameterLayout())
{
    hfGainParam_      = apvts_.getRawParameterValue ("hfGain");
    midFreqParam_     = apvts_.getRawParameterValue ("midFreq");
    midGainParam_     = apvts_.getRawParameterValue ("midGain");
    lowFreqParam_     = apvts_.getRawParameterValue ("lowFreq");
    lowGainParam_     = apvts_.getRawParameterValue ("lowGain");
    hpfFreqParam_     = apvts_.getRawParameterValue ("hpfFreq");
    outputTrimParam_  = apvts_.getRawParameterValue ("outputTrim");
    eqInParam_        = apvts_.getRawParameterValue ("eqIn");
    phaseInvertParam_ = apvts_.getRawParameterValue ("phaseInvert");
    oversampleParam_  = apvts_.getRawParameterValue ("oversampling");
}

juce::AudioProcessorValueTreeState::ParameterLayout Neve1073EQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hfGain", 1 },
        "HF 12 kHz Shelf",
        juce::NormalisableRange<float> (-16.0f, 16.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "midFreq", 1 },
        "Mid Frequency",
        juce::StringArray { "OFF", "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" },
        3));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "midGain", 1 },
        "Mid Bell Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lowFreq", 1 },
        "Low Shelf Frequency",
        juce::StringArray { "OFF", "35 Hz", "60 Hz", "110 Hz", "220 Hz" },
        2));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lowGain", 1 },
        "Low Shelf Gain",
        juce::NormalisableRange<float> (-16.0f, 16.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "hpfFreq", 1 },
        "HPF Cutoff (18 dB/oct)",
        juce::StringArray { "OFF", "50 Hz", "80 Hz", "160 Hz", "300 Hz" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "outputTrim", 1 },
        "Output Trim",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "eqIn", 1 },
        "EQ In (EQL)",
        true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "phaseInvert", 1 },
        "Phase Invert",
        false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "oversampling", 1 },
        "Oversampling",
        juce::StringArray { "1x (Off)", "2x", "4x (Cauer)" },
        2));

    return { params.begin(), params.end() };
}

nv73eq::dsp::EQParams Neve1073EQAudioProcessor::readCurrentParams() const noexcept
{
    nv73eq::dsp::EQParams p;
    p.hfGainDb     = static_cast<double> (hfGainParam_->load (std::memory_order_relaxed));
    p.midFreqStep  = static_cast<int> (std::lround (midFreqParam_->load (std::memory_order_relaxed)));
    p.midGainDb    = static_cast<double> (midGainParam_->load (std::memory_order_relaxed));
    p.lowFreqStep  = static_cast<int> (std::lround (lowFreqParam_->load (std::memory_order_relaxed)));
    p.lowGainDb    = static_cast<double> (lowGainParam_->load (std::memory_order_relaxed));
    p.hpfStep      = static_cast<int> (std::lround (hpfFreqParam_->load (std::memory_order_relaxed)));
    p.outputTrimDb = static_cast<double> (outputTrimParam_->load (std::memory_order_relaxed));
    p.eqEnabled    = eqInParam_->load (std::memory_order_relaxed) >= 0.5f;
    p.phaseInvert  = phaseInvertParam_->load (std::memory_order_relaxed) >= 0.5f;

    const int osIdx = static_cast<int> (std::lround (oversampleParam_->load (std::memory_order_relaxed)));
    if (osIdx == 0)
        p.oversampling = nv73::dsp::Oversampler::Factor::x1;
    else if (osIdx == 1)
        p.oversampling = nv73::dsp::Oversampler::Factor::x2;
    else
        p.oversampling = nv73::dsp::Oversampler::Factor::x4;

    return p;
}

void Neve1073EQAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    dsp_.prepare (sampleRate);
    dsp_.setParameters (readCurrentParams(), 1.0, true);
}

void Neve1073EQAudioProcessor::releaseResources()
{
    dsp_.reset();
}

bool Neve1073EQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainOut == layouts.getMainInputChannelSet();
}

void Neve1073EQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    dsp_.setParameters (readCurrentParams(), 1.0, false);
    dsp_.processBlock (buffer.getArrayOfWritePointers(),
                       buffer.getNumChannels(),
                       buffer.getNumSamples());
}

juce::AudioProcessorEditor* Neve1073EQAudioProcessor::createEditor()
{
    return new Neve1073EQAudioProcessorEditor (*this);
}

void Neve1073EQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void Neve1073EQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName (apvts_.state.getType()))
    {
        apvts_.replaceState (juce::ValueTree::fromXml (*xmlState));
        dsp_.setParameters (readCurrentParams(), 1.0, true);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Neve1073EQAudioProcessor();
}
