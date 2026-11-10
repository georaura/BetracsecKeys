// BeTracks Keys — controles de la sección FM (partes FM-X de MONTAGE/MODX).
#pragma once

#include "OrganPanel.h"

class FmPanel
{
public:
    FmPanel (juce::Component& parent, KeysProcessor& proc);
    ~FmPanel();

    std::vector<OrganPanel::Group> groups (int knobW);
    void setVisible (bool visible);
    /** Lista de partes de la performance actual. */
    void update();

private:
    juce::Component& parent_;
    KeysProcessor& proc_;
    juce::Label partsLabel_, parts_;
    juce::ToggleButton normalize_ { "Igualar volumen" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> normalizeAttachment_;
    std::unique_ptr<Knob> bright_, attack_, release_, level_;
    std::vector<juce::Component*> all_;
    juce::String shown_;
};
