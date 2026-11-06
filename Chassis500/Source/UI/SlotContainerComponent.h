#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace lbx500::ui
{

/**
 * Single vertical 500-series rack bay (Slots 1..6) inside the LBX-500 Lunchbox.
 * Supports:
 *   - Module dropdown selection (or empty bay showing internal 15-pin VPR gold edge connector)
 *   - Drag-and-drop slot swapping (drag top header bar onto any other slot!) + < / > step buttons
 *   - Per-slot hard-wire relay Bypass (BYP) and Solo (S)
 */
class SlotContainerComponent final : public juce::Component,
                                     public juce::DragAndDropTarget
{
public:
    SlotContainerComponent (LBX500ChassisAudioProcessor& proc, int slotIndex)
        : proc_ (proc), slotIndex_ (slotIndex)
    {
        // Populate module selector
        moduleSelector_.addItem ("[ Empty 500 Bay ]", 1);
        const auto& available = sdk500::ModuleRegistry::getAvailableModules();
        for (std::size_t i = 0; i < available.size(); ++i)
        {
            const auto& d = available[i].descriptor;
            moduleSelector_.addItem (d.shortCode + " (" + juce::String (static_cast<int> (d.idleCurrentMa)) + "mA)",
                                     static_cast<int> (i) + 2);
        }

        moduleSelector_.onChange = [this]
        {
            if (ignoreSelectorCallback_)
                return;
            const int selId = moduleSelector_.getSelectedId();
            if (selId <= 1)
                proc_.setSlotModule (slotIndex_, "");
            else
            {
                const auto& mods = sdk500::ModuleRegistry::getAvailableModules();
                const std::size_t idx = static_cast<std::size_t> (selId - 2);
                if (idx < mods.size())
                    proc_.setSlotModule (slotIndex_, mods[idx].descriptor.id);
            }
            rebuildFromProcessor();
        };
        addAndMakeVisible (moduleSelector_);

        moveLeftBtn_.setEnabled (slotIndex_ > 0);
        moveLeftBtn_.onClick = [this]
        {
            if (slotIndex_ > 0)
                proc_.swapSlots (slotIndex_, slotIndex_ - 1);
        };
        addAndMakeVisible (moveLeftBtn_);

        moveRightBtn_.setEnabled (slotIndex_ + 1 < LBX500ChassisAudioProcessor::kNumSlots);
        moveRightBtn_.onClick = [this]
        {
            if (slotIndex_ + 1 < LBX500ChassisAudioProcessor::kNumSlots)
                proc_.swapSlots (slotIndex_, slotIndex_ + 1);
        };
        addAndMakeVisible (moveRightBtn_);

        bypBtn_.setClickingTogglesState (true);
        bypBtn_.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd97706));
        bypBtn_.onClick = [this] { proc_.setSlotBypassed (slotIndex_, bypBtn_.getToggleState()); repaint(); };
        addAndMakeVisible (bypBtn_);

        soloBtn_.setClickingTogglesState (true);
        soloBtn_.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffeab308));
        soloBtn_.onClick = [this] { proc_.setSlotSoloed (slotIndex_, soloBtn_.getToggleState()); repaint(); };
        addAndMakeVisible (soloBtn_);

        quickInsertBtn_.onClick = [this]
        {
            proc_.setSlotModule (slotIndex_, "nv73lb");
            rebuildFromProcessor();
        };
        addChildComponent (quickInsertBtn_);

        rebuildFromProcessor();
    }

    void rebuildFromProcessor()
    {
        auto currentMod = proc_.getSlotModule (slotIndex_);
        bypBtn_.setToggleState  (proc_.isSlotBypassed (slotIndex_), juce::dontSendNotification);
        soloBtn_.setToggleState (proc_.isSlotSoloed (slotIndex_),   juce::dontSendNotification);

        if (currentMod != activeModulePtr_)
        {
            moduleEditor_.reset();
            activeModulePtr_ = currentMod;

            ignoreSelectorCallback_ = true;
            if (activeModulePtr_ == nullptr)
            {
                moduleSelector_.setSelectedId (1, juce::dontSendNotification);
                quickInsertBtn_.setVisible (true);
            }
            else
            {
                quickInsertBtn_.setVisible (false);
                const auto& mods = sdk500::ModuleRegistry::getAvailableModules();
                int foundId = 1;
                for (std::size_t i = 0; i < mods.size(); ++i)
                    if (mods[i].descriptor.id == activeModulePtr_->getDescriptor().id)
                        foundId = static_cast<int> (i) + 2;
                moduleSelector_.setSelectedId (foundId, juce::dontSendNotification);

                moduleEditor_ = activeModulePtr_->createSlotEditor();
                if (moduleEditor_ != nullptr)
                    addAndMakeVisible (*moduleEditor_);
            }
            ignoreSelectorCallback_ = false;
            resized();
        }
        repaint();
    }

    // --- Drag-and-Drop Slot Swapping ---
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getMouseDownY() <= 26 && e.getDistanceFromDragStart() > 5)
        {
            if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
                if (! dnd->isDragAndDropActive())
                    dnd->startDragging ("LBX500_SLOT:" + juce::String (slotIndex_), this);
        }
    }

    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return details.description.toString().startsWith ("LBX500_SLOT:");
    }

    void itemDragEnter (const SourceDetails&) override { isDragHover_ = true;  repaint(); }
    void itemDragExit  (const SourceDetails&) override { isDragHover_ = false; repaint(); }

    void itemDropped (const SourceDetails& details) override
    {
        isDragHover_ = false;
        const auto desc = details.description.toString();
        if (desc.startsWith ("LBX500_SLOT:"))
        {
            const int srcSlot = desc.fromFirstOccurrenceOf ("LBX500_SLOT:", false, false).getIntValue();
            if (srcSlot != slotIndex_)
                proc_.swapSlots (srcSlot, slotIndex_);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();

        // Top slot control header background
        auto header = b.withHeight (54.0f);
        g.setColour (juce::Colour (0xff1a1f24));
        g.fillRoundedRectangle (header, 4.0f);

        // Drag grip title ("SLOT N  :::")
        g.setColour (juce::Colour (0xffd8dde2));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("SLOT " + juce::String (slotIndex_ + 1) + " \xe2\x89\xa1",
                    30, 3, getWidth() - 88, 18, juce::Justification::centredLeft);

        // Empty bay interior: recessed Lunchbox cavity with 15-pin gold VPR card-edge socket!
        const auto bayRect = b.withTrimmedTop (56.0f);
        if (activeModulePtr_ == nullptr)
        {
            juce::ColourGradient cavity (juce::Colour (0xff0d1014), bayRect.getCentreX(), bayRect.getY(),
                                         juce::Colour (0xff181d23), bayRect.getX(),       bayRect.getBottom(), false);
            g.setGradientFill (cavity);
            g.fillRoundedRectangle (bayRect, 3.0f);

            g.setColour (juce::Colour (0xff2e3640));
            g.drawRoundedRectangle (bayRect.reduced (0.5f), 3.0f, 1.0f);

            // Draw 15-pin VPR Alliance gold card-edge socket on backplane PCB!
            const float connW = 18.0f, connH = 148.0f;
            const juce::Rectangle<float> edgeSocket (bayRect.getCentreX() - connW * 0.5f,
                                                     bayRect.getCentreY() - connH * 0.5f - 18.0f,
                                                     connW, connH);
            g.setColour (juce::Colour (0xff1d3b2a)); // Dark green backplane PCB
            g.fillRoundedRectangle (edgeSocket.expanded (6.0f, 10.0f), 2.0f);
            g.setColour (juce::Colour (0xff090b0d));
            g.fillRect (edgeSocket);

            for (int pin = 0; pin < 15; ++pin)
            {
                const float py = edgeSocket.getY() + 5.0f + pin * ((connH - 10.0f) / 14.0f);
                g.setColour (juce::Colour (0xffd4af37).withAlpha (0.72f)); // Gold VPR fingers
                g.fillRect (edgeSocket.getX() + 2.0f, py - 1.2f, 5.0f, 2.4f);
                g.fillRect (edgeSocket.getRight() - 7.0f, py - 1.2f, 5.0f, 2.4f);
            }

            g.setColour (juce::Colour (0xff6f7c88));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("VPR 15-PIN +/-16V BUS", bayRect.withTop (edgeSocket.getBottom() + 14.0f).withHeight (14.0f).toNearestInt(),
                        juce::Justification::centred);
        }

        // Dim overlay if slot is bypassed
        if (activeModulePtr_ != nullptr && proc_.isSlotBypassed (slotIndex_))
        {
            g.setColour (juce::Colours::black.withAlpha (0.32f));
            g.fillRoundedRectangle (bayRect, 3.0f);
        }

        if (isDragHover_)
        {
            g.setColour (juce::Colour (0xfff5a623).withAlpha (0.85f));
            g.drawRoundedRectangle (b.reduced (1.5f), 4.0f, 2.5f);
        }
    }

    void resized() override
    {
        const int w = getWidth();
        moveLeftBtn_.setBounds  (3,  3, 22, 18);
        bypBtn_.setBounds       (w - 76, 3, 30, 18);
        soloBtn_.setBounds      (w - 44, 3, 20, 18);
        moveRightBtn_.setBounds (w - 22, 3, 19, 18);

        moduleSelector_.setBounds (3, 25, w - 6, 24);

        const auto bayRect = getLocalBounds().withTrimmedTop (56);
        if (moduleEditor_ != nullptr)
            moduleEditor_->setBounds (bayRect);

        quickInsertBtn_.setBounds (bayRect.getX() + 14, bayRect.getBottom() - 62, bayRect.getWidth() - 28, 30);
    }

private:
    LBX500ChassisAudioProcessor& proc_;
    int slotIndex_ { 0 };

    std::shared_ptr<sdk500::Module500> activeModulePtr_;
    std::unique_ptr<juce::Component>   moduleEditor_;

    juce::ComboBox   moduleSelector_;
    juce::TextButton moveLeftBtn_    { "<" };
    juce::TextButton moveRightBtn_   { ">" };
    juce::TextButton bypBtn_         { "BYP" };
    juce::TextButton soloBtn_        { "S" };
    juce::TextButton quickInsertBtn_ { "+ Insert NV-73LB" };

    bool ignoreSelectorCallback_ { false };
    bool isDragHover_            { false };
};

} // namespace lbx500::ui
