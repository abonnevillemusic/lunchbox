#include "PluginProcessor.h"
#include "PluginEditor.h"

LBX500ChassisAudioProcessor::LBX500ChassisAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "LBX500_MASTER", createParameterLayout())
{
    inTrimParam_  = apvts_.getRawParameterValue ("inputTrim");
    outTrimParam_ = apvts_.getRawParameterValue ("outputTrim");
    xtalkParam_   = apvts_.getRawParameterValue ("crosstalk");
    psuModeParam_ = apvts_.getRawParameterValue ("psuMode");
    term600Param_ = apvts_.getRawParameterValue ("terminate600");

    // Pre-populate Slot 1 with our NV-73LB Class-A Preamp so a new Lunchbox instance is ready to play!
    setSlotModule (0, "nv73lb");
}

juce::AudioProcessorValueTreeState::ParameterLayout LBX500ChassisAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "inputTrim", 1 },
        "Chassis Input Trim",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "outputTrim", 1 },
        "Chassis Master Out",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "crosstalk", 1 },
        "Backplane Coupling",
        juce::NormalisableRange<float> (0.0f, 200.0f, 1.0f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "psuMode", 1 },
        "PSU Topology",
        juce::StringArray { "Vintage Linear (Shared +/-16V)", "Isolated OST (Regulated)" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "terminate600", 1 },
        "600-Ohm Output Load",
        false));

    return { params.begin(), params.end() };
}

lbx500::dsp::ChassisMasterParams LBX500ChassisAudioProcessor::readMasterParams() const noexcept
{
    lbx500::dsp::ChassisMasterParams p;
    p.inputTrimDb     = static_cast<double> (inTrimParam_->load (std::memory_order_relaxed));
    p.outputTrimDb    = static_cast<double> (outTrimParam_->load (std::memory_order_relaxed));
    p.crosstalkScale  = static_cast<double> (xtalkParam_->load (std::memory_order_relaxed)) * 0.01;
    const int psuIdx  = static_cast<int> (std::lround (psuModeParam_->load (std::memory_order_relaxed)));
    p.psuTopology     = (psuIdx == 0) ? lbx500::dsp::PsuTopology::VintageLinearShared
                                      : lbx500::dsp::PsuTopology::IsolatedOSTRegulated;
    p.terminateOut600 = term600Param_->load (std::memory_order_relaxed) >= 0.5f;
    return p;
}

void LBX500ChassisAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = sampleRate;
    currentBlockSize_  = samplesPerBlock;

    backplane_.prepare (sampleRate);
    backplane_.setMasterParams (readMasterParams(), true);

    std::array<lbx500::dsp::SlotRuntimeEntry, kNumSlots> snap;
    {
        const juce::SpinLock::ScopedLockType lock (slotLock_);
        snap = slots_;
    }
    for (auto& s : snap)
        if (s.module != nullptr)
            s.module->prepare (sampleRate, samplesPerBlock);
}

void LBX500ChassisAudioProcessor::releaseResources()
{
    backplane_.reset();
}

bool LBX500ChassisAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void LBX500ChassisAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    backplane_.setMasterParams (readMasterParams(), false);

    // Copy 6 shared_ptrs in ~25ns under try-lock; if UI is mid-swap, pass clean buffer or last snapshot
    std::array<lbx500::dsp::SlotRuntimeEntry, kNumSlots> localSlots;
    {
        const juce::SpinLock::ScopedTryLockType tryLock (slotLock_);
        if (! tryLock.isLocked())
            return;
        localSlots = slots_;
    }

    backplane_.processBlock (buffer.getArrayOfWritePointers(),
                             buffer.getNumChannels(),
                             buffer.getNumSamples(),
                             localSlots);
}

void LBX500ChassisAudioProcessor::setSlotModule (int slotIndex, const juce::String& moduleId)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return;

    std::shared_ptr<sdk500::Module500> newMod;
    if (moduleId.isNotEmpty())
    {
        if (auto created = sdk500::ModuleRegistry::createById (moduleId))
        {
            created->prepare (currentSampleRate_, currentBlockSize_);
            newMod = std::shared_ptr<sdk500::Module500> (std::move (created));
        }
    }

    std::shared_ptr<sdk500::Module500> oldMod;
    {
        const juce::SpinLock::ScopedLockType lock (slotLock_);
        auto& entry = slots_[static_cast<std::size_t> (slotIndex)];
        oldMod = std::move (entry.module);
        entry.module   = std::move (newMod);
        entry.bypassed = false;
        entry.soloed   = false;
    }

    if (oldMod != nullptr)
        retiredPool_.push_back (std::move (oldMod));

    rackRevision_.fetch_add (1, std::memory_order_release);
}

void LBX500ChassisAudioProcessor::swapSlots (int slotA, int slotB)
{
    if (slotA < 0 || slotA >= kNumSlots || slotB < 0 || slotB >= kNumSlots || slotA == slotB)
        return;

    {
        const juce::SpinLock::ScopedLockType lock (slotLock_);
        std::swap (slots_[static_cast<std::size_t> (slotA)],
                   slots_[static_cast<std::size_t> (slotB)]);
    }
    rackRevision_.fetch_add (1, std::memory_order_release);
}

void LBX500ChassisAudioProcessor::setSlotBypassed (int slotIndex, bool bypassed)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return;
    const juce::SpinLock::ScopedLockType lock (slotLock_);
    slots_[static_cast<std::size_t> (slotIndex)].bypassed = bypassed;
}

void LBX500ChassisAudioProcessor::setSlotSoloed (int slotIndex, bool soloed)
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return;
    const juce::SpinLock::ScopedLockType lock (slotLock_);
    slots_[static_cast<std::size_t> (slotIndex)].soloed = soloed;
}

std::shared_ptr<sdk500::Module500> LBX500ChassisAudioProcessor::getSlotModule (int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return nullptr;
    const juce::SpinLock::ScopedLockType lock (slotLock_);
    return slots_[static_cast<std::size_t> (slotIndex)].module;
}

bool LBX500ChassisAudioProcessor::isSlotBypassed (int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return false;
    const juce::SpinLock::ScopedLockType lock (slotLock_);
    return slots_[static_cast<std::size_t> (slotIndex)].bypassed;
}

bool LBX500ChassisAudioProcessor::isSlotSoloed (int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= kNumSlots)
        return false;
    const juce::SpinLock::ScopedLockType lock (slotLock_);
    return slots_[static_cast<std::size_t> (slotIndex)].soloed;
}

void LBX500ChassisAudioProcessor::collectRetiredModules()
{
    // Destroy any retired module whose sole remaining reference is retiredPool_ (i.e. audio thread done)
    std::erase_if (retiredPool_, [] (const auto& ptr) { return ptr.use_count() <= 1; });
}

juce::AudioProcessorEditor* LBX500ChassisAudioProcessor::createEditor()
{
    return new LBX500ChassisAudioProcessorEditor (*this);
}

void LBX500ChassisAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("LBX500_RACK_STATE");

    if (auto masterXml = apvts_.copyState().createXml())
        root.addChildElement (masterXml.release());

    auto* slotsXml = root.createNewChildElement ("Slots");
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto* sXml = slotsXml->createNewChildElement ("Slot");
        sXml->setAttribute ("index",    i);
        sXml->setAttribute ("bypassed", isSlotBypassed (i));
        sXml->setAttribute ("soloed",   isSlotSoloed (i));

        if (auto mod = getSlotModule (i))
        {
            sXml->setAttribute ("moduleId", mod->getDescriptor().id);
            if (auto modXml = mod->saveStateXml())
                sXml->addChildElement (modXml.release());
        }
        else
        {
            sXml->setAttribute ("moduleId", "");
        }
    }

    copyXmlToBinary (root, destData);
}

void LBX500ChassisAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = getXmlFromBinary (data, sizeInBytes);
    if (root == nullptr || ! root->hasTagName ("LBX500_RACK_STATE"))
        return;

    if (auto* masterXml = root->getChildByName (apvts_.state.getType()))
        apvts_.replaceState (juce::ValueTree::fromXml (*masterXml));

    if (auto* slotsXml = root->getChildByName ("Slots"))
    {
        for (auto* sXml : slotsXml->getChildWithTagNameIterator ("Slot"))
        {
            const int idx = sXml->getIntAttribute ("index", -1);
            if (idx < 0 || idx >= kNumSlots)
                continue;

            const juce::String modId = sXml->getStringAttribute ("moduleId", "");
            setSlotModule (idx, modId);
            setSlotBypassed (idx, sXml->getBoolAttribute ("bypassed", false));
            setSlotSoloed   (idx, sXml->getBoolAttribute ("soloed", false));

            if (auto mod = getSlotModule (idx))
                if (auto* firstChild = sXml->getFirstChildElement())
                    mod->loadStateXml (*firstChild);
        }
    }
    rackRevision_.fetch_add (1, std::memory_order_release);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LBX500ChassisAudioProcessor();
}
