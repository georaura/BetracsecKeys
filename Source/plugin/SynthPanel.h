// BeTracks Keys — controles de la sección de synth.
#pragma once

#include "OrganPanel.h"

class SynthPanel
{
public:
    SynthPanel (juce::Component& parent, KeysProcessor& proc);
    ~SynthPanel();

    std::vector<OrganPanel::Group> groups (int knobW);
    void setVisible (bool visible);
    /** Rótulo del botón de onda (cambia con el oscilador, la onda o el sample). */
    void update();

private:
    void showWaveMenu();
    void setParam (const char* id, float value);

    juce::Component& parent_;
    KeysProcessor& proc_;
    juce::Label oscLabel_, lfoLabel_, voiceLabel_;
    juce::TextButton wave_;
    juce::Label layerLabel_;
    juce::TextButton layers_[3] { juce::TextButton ("A"), juce::TextButton ("B"), juce::TextButton ("C") };
    juce::ToggleButton filterOn_ { "FILTRO" }, mono_ { "Mono" }, legato_ { "Legato" };
    std::unique_ptr<ChoiceButtons> source_, filterType_, track_, lfoTarget_, lfoShape_, unison_;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments_;
    std::unique_ptr<Knob> oscCtrl_, coarse_, cutoff_, reso_, envAmt_, fA_, fD_, fR_, aA_, aD_, aR_, lfoRate_, lfoAmt_, glide_, level_;
    std::vector<juce::Component*> all_;
};
