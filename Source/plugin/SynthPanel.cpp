#include "SynthPanel.h"
#include "PluginEditor.h"
#include "Text.h"

#include <map>

using namespace juce;

SynthPanel::SynthPanel (Component& parent, KeysProcessor& proc) : parent_ (parent), proc_ (proc)
{
    auto& st = proc.params;
    for (auto* l : { &oscLabel_, &lfoLabel_, &voiceLabel_ })
    {
        l->setFont (FontOptions (12.0f, Font::bold));
        l->setJustificationType (Justification::centred);
        all_.push_back (l);
    }
    oscLabel_.setText ("OSCILADOR", dontSendNotification);
    lfoLabel_.setText ("LFO", dontSendNotification);
    voiceLabel_.setText ("VOZ", dontSendNotification);

    source_ = std::make_unique<ChoiceButtons> (*st.getParameter ("synthSource"), StringArray { "Analog", "FM-H", "FM-I", "Wave", "Sample" }, 5);
    filterType_ = std::make_unique<ChoiceButtons> (*st.getParameter ("synthFilterType"), StringArray { "LP12", "LP24", "LPM", "LP+HP", "BP", "HP" }, 3);
    track_ = std::make_unique<ChoiceButtons> (*st.getParameter ("synthTrack"), StringArray { "Off", "1/3", "2/3", "1" }, 4);
    lfoTarget_ = std::make_unique<ChoiceButtons> (*st.getParameter ("synthLfoTarget"), StringArray { "Off", "Osc", "Tono", "Filtro" }, 4);
    lfoShape_ = std::make_unique<ChoiceButtons> (*st.getParameter ("synthLfoShape"), StringArray { "Tri", "Saw", "Inv", "Sq", "S/H" }, 5);
    unison_ = std::make_unique<ChoiceButtons> (*st.getParameter ("synthUnison"), StringArray { "Off", "1", "2", "3" }, 4);
    for (Component* c : { (Component*) source_.get(), (Component*) filterType_.get(), (Component*) track_.get(),
                          (Component*) lfoTarget_.get(), (Component*) lfoShape_.get(), (Component*) unison_.get() })
        all_.push_back (c);
    source_->setTooltip (String::fromUTF8 ("Tipo de oscilador. «Wave» es una aproximación: las ondas originales no se incluyen."));
    track_->setTooltip (String::fromUTF8 ("Seguimiento de teclado del filtro."));
    lfoShape_->setTooltip (String::fromUTF8 ("Forma del LFO: triángulo, sierra, sierra invertida, cuadrada y muestreo aleatorio."));
    unison_->setTooltip (String::fromUTF8 ("Unison: voces desafinadas que engordan el sonido."));

    const std::pair<ToggleButton*, const char*> toggles[] = { { &filterOn_, "synthFilterOn" }, { &mono_, "synthMono" }, { &legato_, "synthLegato" } };
    for (auto& [b, pid] : toggles)
    {
        buttonAttachments_.add (new AudioProcessorValueTreeState::ButtonAttachment (st, pid, *b));
        all_.push_back (b);
    }

    layerLabel_.setText ("CAPA", dontSendNotification);
    layerLabel_.setFont (FontOptions (11.0f, Font::bold));
    layerLabel_.setColour (Label::textColourId, KeysLookAndFeel::dim);
    all_.push_back (&layerLabel_);
    for (int i = 0; i < 3; ++i)
    {
        layers_[i].setClickingTogglesState (false);
        layers_[i].setTooltip (String::fromUTF8 ("Capa del preset que toca Keys (el instrumento original puede sonar con varias a la vez)."));
        layers_[i].onClick = [this, i]
        {
            if (proc_.currentSynthPreset().existsAsFile()) proc_.loadSynthPreset (proc_.currentSynthPreset(), i);
        };
        all_.push_back (&layers_[i]);
    }
    wave_.onClick = [this] { showWaveMenu(); };
    wave_.setTooltip (String::fromUTF8 ("Elige la onda, la relación FM o el sample."));
    all_.push_back (&wave_);

    auto knob = [&] (std::unique_ptr<Knob>& k, const char* pid, const char* label)
    {
        k = std::make_unique<Knob> (st, pid, String::fromUTF8 (label));
        all_.push_back (k.get());
    };
    knob (oscCtrl_, "synthOscCtrl", "OSC CTRL");
    knob (coarse_, "synthCoarse", "TONO");
    knob (cutoff_, "synthCutoff", "FREQ");
    knob (reso_, "synthReso", "RESO");
    knob (envAmt_, "synthFEnvAmt", "ENV");
    knob (fA_, "synthFEnvA", "F. ATAQUE");
    knob (fD_, "synthFEnvD", "F. DECAY");
    knob (fR_, "synthFEnvR", "F. RELEASE");
    knob (aA_, "synthAmpA", "ATAQUE");
    knob (aD_, "synthAmpD", "DECAY");
    knob (aR_, "synthAmpR", "RELEASE");
    knob (lfoRate_, "synthLfoRate", "RATE");
    knob (lfoAmt_, "synthLfoAmt", "CANTIDAD");
    knob (glide_, "synthGlide", "GLIDE");
    knob (level_, "synthVolume", "NIVEL");
    oscCtrl_->slider.setTooltip (String::fromUTF8 ("Control del oscilador: forma, mezcla, sync o índice de FM según la onda."));
    reso_->slider.setTooltip (String::fromUTF8 ("Resonancia. En LP+HP es el corte del paso alto."));

    for (auto* c : all_) parent_.addChildComponent (c);
    update();
}

SynthPanel::~SynthPanel() = default;

void SynthPanel::setVisible (bool visible)
{
    for (auto* c : all_) c->setVisible (visible);
}

void SynthPanel::setParam (const char* id, float value)
{
    if (auto* p = proc_.params.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

static String fmRatioText (bool harmonic, int w)
{
    const double r = harmonic ? bk::synth::fmHarmonicRatio (w) : bk::synth::fmInharmonicRatio (w);
    return String::fromUTF8 ("×") + String (r, harmonic ? (w == 0 ? 1 : 0) : 2);
}

void SynthPanel::update()
{
    const auto p = proc_.synthParamsFromState();
    String text;
    switch (p.source)
    {
        case bk::synth::Source::Analog:
            if (const char* n = bk::synth::analogWaveName (p.category, p.wave)) text = n;
            else text = bk::synth::analogCategoryName (p.category);
            break;
        case bk::synth::Source::FmH: text = "FM-H " + fmRatioText (true, p.wave); break;
        case bk::synth::Source::FmI: text = "FM-I " + fmRatioText (false, p.wave); break;
        case bk::synth::Source::Wave: text = "Wave " + String (p.wave + 1); break;
        case bk::synth::Source::Sample:
        {
            const auto f = proc_.currentSynthSample();
            text = f.existsAsFile() ? f.getFileNameWithoutExtension() : String::fromUTF8 ("Elegir sample…");
            break;
        }
    }
    text += String::fromUTF8 ("  ▾");
    const int on = proc_.currentSynthPreset() != File() ? proc_.synthLayersOn() : 0;
    for (int i = 0; i < 3; ++i)
    {
        layers_[i].setEnabled (on >> i & 1);
        layers_[i].setToggleState (on != 0 && proc_.synthLayer() == i, dontSendNotification);
    }
    if (wave_.getButtonText() != text) wave_.setButtonText (text);
}

void SynthPanel::showWaveMenu()
{
    const auto p = proc_.synthParamsFromState();
    PopupMenu menu;
    switch (p.source)
    {
        case bk::synth::Source::Analog:
            for (int c = 0; c < 8; ++c)
            {
                PopupMenu sub;
                for (int w = 0; bk::synth::analogWaveName (c, w) != nullptr; ++w)
                    sub.addItem (1 + c * 100 + w, bk::synth::analogWaveName (c, w), true, c == p.category && w == p.wave);
                menu.addSubMenu (bk::synth::analogCategoryName (c), sub, true, nullptr, c == p.category);
            }
            break;
        case bk::synth::Source::FmH:
            for (int w = 0; w <= 24; ++w) menu.addItem (1 + w, fmRatioText (true, w), true, w == p.wave);
            break;
        case bk::synth::Source::FmI:
            for (int w = 0; w <= 60; ++w) menu.addItem (1 + w, fmRatioText (false, w), true, w == p.wave);
            break;
        case bk::synth::Source::Wave:
            for (int w = 0; w <= 60; ++w) menu.addItem (1 + w, "Wave " + String (w + 1), true, w == p.wave);
            break;
        case bk::synth::Source::Sample:
        {
            const auto list = proc_.library.sampleEntries();
            if (list.empty())
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Sample del synth",
                    String::fromUTF8 ("No hay samples (.nsmp3/.nsmp4) en la biblioteca. Añade la carpeta de tu librería de samples."));
                return;
            }
            std::map<String, PopupMenu> groups;
            StringArray order;
            const auto current = proc_.currentSynthSample();
            for (size_t i = 0; i < list.size(); ++i)
            {
                if (! groups.count (list[i].group)) order.add (list[i].group);
                groups[list[i].group].addItem (int (i) + 1, list[i].name, true, list[i].file == current);
            }
            for (const auto& g : order) menu.addSubMenu (g, groups[g]);
            menu.showMenuAsync (PopupMenu::Options().withTargetComponent (wave_), [this, list] (int r)
            {
                if (r > 0) proc_.loadSynthSample (list[size_t (r - 1)].file);
            });
            return;
        }
    }
    const bool analog = p.source == bk::synth::Source::Analog;
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (wave_).withMaximumNumColumns (4), [this, analog] (int r)
    {
        if (r <= 0) return;
        if (analog) { setParam ("synthCategory", float ((r - 1) / 100)); setParam ("synthWave", float ((r - 1) % 100)); }
        else setParam ("synthWave", float (r - 1));
        update();
    });
}

std::vector<OrganPanel::Group> SynthPanel::groups (int knobW)
{
    std::vector<OrganPanel::Group> g;
    g.push_back ({ 280 + 2 * knobW, [this, knobW] (Rectangle<int> a)
    {
        auto left = a.removeFromLeft (280).withTrimmedRight (8);
        oscLabel_.setBounds (left.removeFromTop (16));
        left.removeFromTop (8);
        source_->setBounds (left.removeFromTop (26));
        left.removeFromTop (8);
        wave_.setBounds (left.removeFromTop (30));
        left.removeFromTop (8);
        auto row = left.removeFromTop (24);
        layerLabel_.setBounds (row.removeFromLeft (44));
        for (auto& b : layers_) { b.setBounds (row.removeFromLeft (36)); row.removeFromLeft (4); }
        oscCtrl_->setBounds (a.removeFromLeft (knobW));
        coarse_->setBounds (a.removeFromLeft (knobW));
    } });
    g.push_back ({ 168 + 3 * knobW, [this, knobW] (Rectangle<int> a)
    {
        auto left = a.removeFromLeft (168).withTrimmedRight (8);
        filterOn_.setBounds (left.removeFromTop (22));
        left.removeFromTop (4);
        filterType_->setBounds (left.removeFromTop (52));
        left.removeFromTop (8);
        track_->setBounds (left.removeFromTop (22));
        for (auto* k : { cutoff_.get(), reso_.get(), envAmt_.get() }) k->setBounds (a.removeFromLeft (knobW));
    } });
    g.push_back ({ 3 * knobW, [this, knobW] (Rectangle<int> a)
        { for (auto* k : { fA_.get(), fD_.get(), fR_.get() }) k->setBounds (a.removeFromLeft (knobW)); } });
    g.push_back ({ 3 * knobW, [this, knobW] (Rectangle<int> a)
        { for (auto* k : { aA_.get(), aD_.get(), aR_.get() }) k->setBounds (a.removeFromLeft (knobW)); } });
    g.push_back ({ 210 + 2 * knobW, [this, knobW] (Rectangle<int> a)
    {
        auto left = a.removeFromLeft (210).withTrimmedRight (8);
        lfoLabel_.setBounds (left.removeFromTop (16));
        left.removeFromTop (8);
        lfoTarget_->setBounds (left.removeFromTop (24));
        left.removeFromTop (8);
        lfoShape_->setBounds (left.removeFromTop (24));
        lfoRate_->setBounds (a.removeFromLeft (knobW));
        lfoAmt_->setBounds (a.removeFromLeft (knobW));
    } });
    g.push_back ({ 172 + 2 * knobW, [this, knobW] (Rectangle<int> a)
    {
        auto left = a.removeFromLeft (172).withTrimmedRight (8);
        voiceLabel_.setBounds (left.removeFromTop (16));
        left.removeFromTop (8);
        unison_->setBounds (left.removeFromTop (24));
        left.removeFromTop (8);
        auto row = left.removeFromTop (24);
        mono_.setBounds (row.removeFromLeft (row.getWidth() / 2));
        legato_.setBounds (row);
        glide_->setBounds (a.removeFromLeft (knobW));
        level_->setBounds (a.removeFromLeft (knobW));
    } });
    return g;
}
