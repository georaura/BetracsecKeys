#include "FmPanel.h"
#include "PluginEditor.h"
#include "Text.h"

using namespace juce;

namespace
{
String noteName (int n) { return MidiMessage::getMidiNoteName (n, true, true, 3); }
}

FmPanel::FmPanel (Component& parent, KeysProcessor& proc) : parent_ (parent), proc_ (proc)
{
    auto& st = proc.params;
    partsLabel_.setText ("PARTES", dontSendNotification);
    partsLabel_.setFont (FontOptions (12.0f, Font::bold));
    partsLabel_.setJustificationType (Justification::centredLeft);
    parts_.setFont (FontOptions (12.5f));
    parts_.setJustificationType (Justification::topLeft);
    parts_.setColour (Label::textColourId, KeysLookAndFeel::dim);
    all_.push_back (&partsLabel_);
    all_.push_back (&parts_);
    normalize_.setTooltip (String::fromUTF8 ("Lleva cada performance a un volumen parecido. En el instrumento original las equilibran sus efectos, "
                                             "que Keys no tiene; apágalo para oír el volumen guardado en la librería."));
    normalizeAttachment_ = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (st, "fmNormalize", normalize_);
    all_.push_back (&normalize_);

    auto knob = [&] (std::unique_ptr<Knob>& k, const char* pid, const char* label, const char* tip)
    {
        k = std::make_unique<Knob> (st, pid, String::fromUTF8 (label));
        k->slider.setTooltip (String::fromUTF8 (tip));
        all_.push_back (k.get());
    };
    knob (bright_, "fmBright", "BRILLO", "Partes FM: sube o baja el nivel de los moduladores (más o menos armónicos).");
    knob (attack_, "fmAttack", "ATAQUE", "Partes FM: alarga o acorta el ataque de todos los operadores.");
    knob (release_, "fmRelease", "RELEASE", "Partes FM: alarga o acorta la caída al soltar la tecla.");
    knob (level_, "fmVolume", "NIVEL", "Nivel de la sección FM.");
    for (auto* c : all_) parent_.addChildComponent (c);
}

FmPanel::~FmPanel() = default;

void FmPanel::setVisible (bool visible)
{
    for (auto* c : all_) c->setVisible (visible);
}

void FmPanel::update()
{
    String text;
    if (const auto* lib = proc_.fmLibrary(); lib != nullptr && proc_.currentFmIndex() >= 0
                                               && size_t (proc_.currentFmIndex()) < lib->performances.size())
    {
        const auto& perf = lib->performances[size_t (proc_.currentFmIndex())];
        const auto dot = String::fromUTF8 ("  ·  ");
        auto range = [&] (const bk::ysfc::PartInfo& p, String& line)
        {
            if (p.noteShift != 0) line << dot << (p.noteShift > 0 ? "+" : "") << p.noteShift << " st";
            if (p.noteLo > 0 || p.noteHi < 127) line << dot << noteName (p.noteLo) << String::fromUTF8 ("–") << noteName (p.noteHi);
            if (p.pan < 32) line << dot << "izq."; else if (p.pan > 96) line << dot << "der.";
            if (p.volume == 0 || ! p.partOn) line << "  (muda)";
            else if (! p.keyboard) line << "  (sin teclado)";
        };
        int shown = 0;
        for (const auto& p : perf.fm)
        {
            if (shown == 6) break;
            String line = String::fromUTF8 (p.name.c_str()) + dot + "FM alg " + String (p.algorithm + 1);
            range (p, line);
            text << line << newLine;
            ++shown;
        }
        for (const auto& p : perf.awm)
        {
            if (shown == 6) break;
            int on = 0, ok = 0;
            for (const auto& e : p.elements) if (e.on) { ++on; ok += lib->waveFor (e) != nullptr; }
            String line = String::fromUTF8 (p.name.c_str()) + dot + "muestras " + String (ok) + "/" + String (on) + " elementos";
            range (p, line);
            text << line << newLine;
            ++shown;
        }
        if (int (perf.fm.size() + perf.awm.size()) > shown) text << String::fromUTF8 ("…") << newLine;
        if (perf.romElements > 0)
            text << String::fromUTF8 ("Sin sonido: ") << perf.romElements
                 << String::fromUTF8 (perf.romElements == 1 ? " elemento con onda de la memoria del instrumento" : " elementos con ondas de la memoria del instrumento") << newLine;
        if (perf.drumParts > 0)
            text << String::fromUTF8 ("Sin sonido: ") << perf.drumParts << (perf.drumParts == 1 ? " parte de batería" : " partes de batería") << newLine;
    }
    else text = String::fromUTF8 ("Abre una librería FM (.X7L, .X8L) con «Abrir…» o añade su carpeta en Biblioteca.");
    if (text != shown_) { shown_ = text; parts_.setText (text, dontSendNotification); }
}

std::vector<OrganPanel::Group> FmPanel::groups (int knobW)
{
    std::vector<OrganPanel::Group> g;
    g.push_back ({ 340, [this] (Rectangle<int> a)
    {
        auto top = a.removeFromTop (16);
        normalize_.setBounds (top.removeFromRight (150));
        partsLabel_.setBounds (top);
        a.removeFromTop (6);
        parts_.setBounds (a);
    } });
    g.push_back ({ 4 * knobW, [this, knobW] (Rectangle<int> a)
        { for (auto* k : { bright_.get(), attack_.get(), release_.get(), level_.get() }) k->setBounds (a.removeFromLeft (knobW)); } });
    return g;
}
