// BeTracks Keys — controles de la sección de órgano.
#pragma once

#include "PluginProcessor.h"

struct Knob;

/** Un drawbar: se "saca" hacia abajo (0 arriba, 8 abajo), como en un órgano de ruedas. */
class DrawbarControl : public juce::Component
{
public:
    DrawbarControl (juce::RangedAudioParameter& param, juce::Colour cap, juce::String footage);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    float valueAt (float y) const;
    juce::Rectangle<float> slot() const;
    juce::ParameterAttachment attachment_;
    juce::Colour cap_;
    juce::String footage_;
    float value_ = 0;
};

/** Botones que eligen un valor de un parámetro de opciones (modelo, tipo de vibrato…). */
class ChoiceButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceButtons (juce::RangedAudioParameter& param, juce::StringArray labels, int columns);
    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> buttons_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    int columns_;
};

class OrganPanel
{
public:
    OrganPanel (juce::Component& parent, KeysProcessor& proc);
    ~OrganPanel();

    /** Grupos para la disposición en filas del editor: ancho y cómo colocarse. */
    struct Group { int width; std::function<void (juce::Rectangle<int>)> place; };
    std::vector<Group> groups (int knobW);
    void setVisible (bool visible);

private:
    juce::Component& parent_;
    juce::OwnedArray<DrawbarControl> drawbars_;
    juce::OwnedArray<juce::Label> labels_;
    juce::Label drawbarLabel_, modelLabel_, percLabel_, rotaryLabel_;
    std::unique_ptr<ChoiceButtons> model_, vibType_, rotarySpeed_;
    juce::ToggleButton vib_ { "Vibrato/Chorus" }, sustain_ { "Sustain" };
    juce::ToggleButton percOn_ { "On" }, perc3rd_ { "3rd" }, percFast_ { "Fast" }, percSoft_ { "Suave" }, rotaryOn_ { "On" };
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments_;
    std::unique_ptr<Knob> volume_, click_, drive_;
    std::vector<juce::Component*> all_;
};
