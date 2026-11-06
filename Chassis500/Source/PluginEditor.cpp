#include "PluginEditor.h"

LBX500ChassisAudioProcessorEditor::LBX500ChassisAudioProcessorEditor (LBX500ChassisAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef_ (p),
      masterBay_ (p)
{
    for (int i = 0; i < LBX500ChassisAudioProcessor::kNumSlots; ++i)
    {
        auto slotComp = std::make_unique<lbx500::ui::SlotContainerComponent> (processorRef_, i);
        addAndMakeVisible (*slotComp);
        slotUIs_[static_cast<std::size_t> (i)] = std::move (slotComp);
    }

    addAndMakeVisible (masterBay_);

    lastSeenRevision_ = processorRef_.getRackRevision();
    setSize (1188, 548);
    startTimerHz (24);
}

LBX500ChassisAudioProcessorEditor::~LBX500ChassisAudioProcessorEditor()
{
    stopTimer();
}

void LBX500ChassisAudioProcessorEditor::timerCallback()
{
    processorRef_.collectRetiredModules();

    const auto rev = processorRef_.getRackRevision();
    if (rev != lastSeenRevision_)
    {
        lastSeenRevision_ = rev;
        for (auto& s : slotUIs_)
            if (s != nullptr)
                s->rebuildFromProcessor();
    }

    const auto& bp = processorRef_.getBackplaneDSP();
    masterBay_.updateTelemetry (bp.getTotalCurrentMa(),
                                bp.getRailVoltageVolts(),
                                bp.getMasterInMeter(),
                                bp.getMasterOutMeter());
}

void LBX500ChassisAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // Industrial powder-coated steel Lunchbox chassis exterior
    g.fillAll (juce::Colour (0xff111519));

    // Left & Right brushed extruded aluminum Lunchbox rack ears
    g.setColour (juce::Colour (0xff67727d));
    g.fillRoundedRectangle (4.0f, 8.0f, 14.0f, b.getHeight() - 16.0f, 3.0f);
    g.fillRoundedRectangle (b.getWidth() - 18.0f, 8.0f, 14.0f, b.getHeight() - 16.0f, 3.0f);

    // Top & Bottom Horizontal 500-Series Extruded Mounting Rails
    juce::ColourGradient railGrad (juce::Colour (0xff323b44), 0.0f, 0.0f,
                                   juce::Colour (0xff20262c), 0.0f, 34.0f, false);
    g.setGradientFill (railGrad);
    g.fillRect (20.0f, 6.0f, b.getWidth() - 40.0f, 30.0f);
    g.fillRect (20.0f, b.getHeight() - 20.0f, b.getWidth() - 40.0f, 14.0f);

    g.setColour (juce::Colour (0xffe8e4d8));
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.drawText ("LBX-500  6-SLOT VPR LUNCHBOX CHASSIS", 30, 9, 430, 24, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff9aa6b2));
    g.setFont (juce::FontOptions (11.0f, juce::Font::plain));
    g.drawText ("DRAG SLOT HEADER OR CLICK < / > TO REORDER  \xe2\x80\xa2  SHARED +/-16V 780mA BACKPLANE & MAGNETIC BUS",
                460, 9, 690, 24, juce::Justification::centredRight);
}

void LBX500ChassisAudioProcessorEditor::resized()
{
    const int topY   = 40;
    const int slotH  = getHeight() - 64;
    const int startX = 24;
    const int slotW  = 150;
    const int gap    = 5;

    for (int i = 0; i < LBX500ChassisAudioProcessor::kNumSlots; ++i)
    {
        const int x = startX + i * (slotW + gap);
        slotUIs_[static_cast<std::size_t> (i)]->setBounds (x, topY, slotW, slotH);
    }

    const int masterX = startX + LBX500ChassisAudioProcessor::kNumSlots * (slotW + gap) + 4;
    masterBay_.setBounds (masterX, topY, getWidth() - masterX - 24, slotH);
}
