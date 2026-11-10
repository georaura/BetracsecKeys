#include "OrganPanel.h"
#include "PluginEditor.h"

using namespace juce;

// ------------------------------------------------------------------------------------ drawbar

DrawbarControl::DrawbarControl (RangedAudioParameter& param, Colour cap, String footage)
    : attachment_ (param, [this] (float v) { value_ = v; repaint(); }), cap_ (cap), footage_ (std::move (footage))
{
    attachment_.sendInitialUpdate();
    setRepaintsOnMouseActivity (true);
}

Rectangle<float> DrawbarControl::slot() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromBottom (16); // pie (pies del registro)
    return r.withSizeKeepingCentre (jmin (22.0f, r.getWidth() - 6), r.getHeight());
}

float DrawbarControl::valueAt (float y) const
{
    const auto s = slot();
    const float handle = 22.0f;
    return jlimit (0.0f, 8.0f, std::round ((y - s.getY() - handle * 0.5f) / (s.getHeight() - handle) * 8.0f));
}

void DrawbarControl::paint (Graphics& g)
{
    using L = KeysLookAndFeel;
    const auto s = slot();
    // Ranura con las marcas 1–8.
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (s, 4);
    const float handle = 22.0f;
    const float travel = s.getHeight() - handle;
    g.setColour (Colours::white.withAlpha (0.12f));
    for (int k = 1; k <= 8; ++k)
    {
        const float y = s.getY() + handle * 0.5f + travel * float (k) / 8.0f;
        g.drawHorizontalLine (int (y), s.getX() + 3, s.getRight() - 3);
    }
    // Varilla sacada hasta el valor y la tapa con el número.
    const float pos = s.getY() + travel * value_ / 8.0f;
    g.setColour (Colour (0xff2a2a30));
    g.fillRect (s.withHeight (pos - s.getY() + 2).reduced (6, 0));
    const auto capR = Rectangle<float> (s.getX(), pos, s.getWidth(), handle).reduced (1);
    g.setColour (cap_);
    g.fillRoundedRectangle (capR, 4);
    g.setColour (isMouseOverOrDragging() ? L::accent : Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (capR, 4, 1);
    g.setColour (cap_.getPerceivedBrightness() > 0.5f ? Colours::black : Colours::white);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText (String (roundToInt (value_)), capR, Justification::centred);
    g.setColour (L::dim);
    g.setFont (FontOptions (10.5f));
    g.drawText (footage_, getLocalBounds().removeFromBottom (14), Justification::centred);
}

void DrawbarControl::mouseDown (const MouseEvent& e)
{
    attachment_.beginGesture();
    attachment_.setValueAsPartOfGesture (valueAt (e.position.y));
}

void DrawbarControl::mouseDrag (const MouseEvent& e) { attachment_.setValueAsPartOfGesture (valueAt (e.position.y)); }
void DrawbarControl::mouseUp (const MouseEvent&) { attachment_.endGesture(); }
void DrawbarControl::mouseDoubleClick (const MouseEvent&) { attachment_.setValueAsCompleteGesture (value_ > 0 ? 0.0f : 8.0f); }

// ------------------------------------------------------------------------------------ opciones

ChoiceButtons::ChoiceButtons (RangedAudioParameter& param, StringArray labels, int columns) : columns_ (columns)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons_.add (new TextButton (labels[i]));
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    attachment_ = std::make_unique<ParameterAttachment> (param, [this] (float v)
    {
        for (int i = 0; i < buttons_.size(); ++i) buttons_[i]->setToggleState (i == roundToInt (v), dontSendNotification);
    });
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->onClick = [this, i] { attachment_->setValueAsCompleteGesture (float (i)); };
    attachment_->sendInitialUpdate();
}

void ChoiceButtons::resized()
{
    const int rows = (buttons_.size() + columns_ - 1) / columns_;
    const int h = (getHeight() - (rows - 1) * 4) / rows;
    const int w = (getWidth() - (columns_ - 1) * 4) / columns_;
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setBounds ((i % columns_) * (w + 4), (i / columns_) * (h + 4), w, h);
}

// ------------------------------------------------------------------------------------ panel

OrganPanel::OrganPanel (Component& parent, KeysProcessor& proc) : parent_ (parent)
{
    auto& st = proc.params;
    // Colores clásicos: graves marrones, armónicos pares blancos, mutaciones negras.
    const Colour brown (0xff8a5a3c), white (0xffe8e8ea), black (0xff1e1e22);
    const Colour caps[9] = { brown, brown, white, white, black, white, black, black, white };
    const char* footages[9] = { "16'", "5⅓'", "8'", "4'", "2⅔'", "2'", "1⅗'", "1⅓'", "1'" };
    for (int i = 0; i < 9; ++i)
        all_.push_back (drawbars_.add (new DrawbarControl (*st.getParameter ("drawbar" + String (i + 1)), caps[i], String::fromUTF8 (footages[i]))));

    for (auto* l : { &drawbarLabel_, &modelLabel_, &percLabel_, &rotaryLabel_ })
    {
        l->setFont (FontOptions (12.0f, Font::bold));
        l->setJustificationType (Justification::centred);
        all_.push_back (l);
    }
    drawbarLabel_.setText ("DRAWBARS", dontSendNotification);
    modelLabel_.setText ("MODELO", dontSendNotification);
    percLabel_.setText (String::fromUTF8 ("PERCUSIÓN"), dontSendNotification);
    rotaryLabel_.setText ("ROTARY", dontSendNotification);

    model_ = std::make_unique<ChoiceButtons> (*st.getParameter ("organModel"), StringArray { "B3", "Vox", "Farf", "Pipe 1", "Pipe 2", "Bass" }, 3);
    vibType_ = std::make_unique<ChoiceButtons> (*st.getParameter ("organVibType"), StringArray { "V1", "V2", "V3", "C1", "C2", "C3" }, 6);
    rotarySpeed_ = std::make_unique<ChoiceButtons> (*st.getParameter ("rotaryFast"), StringArray { "Lento", String::fromUTF8 ("Rápido") }, 2);
    for (Component* c : { (Component*) model_.get(), (Component*) vibType_.get(), (Component*) rotarySpeed_.get() }) all_.push_back (c);

    const std::pair<ToggleButton*, const char*> toggles[] = {
        { &vib_, "organVib" }, { &sustain_, "organSustain" }, { &percOn_, "percOn" }, { &perc3rd_, "perc3rd" },
        { &percFast_, "percFast" }, { &percSoft_, "percSoft" }, { &rotaryOn_, "rotaryOn" },
    };
    for (auto& [b, pid] : toggles)
    {
        buttonAttachments_.add (new AudioProcessorValueTreeState::ButtonAttachment (st, pid, *b));
        all_.push_back (b);
    }
    perc3rd_.setButtonText (String::fromUTF8 ("3ª"));
    percFast_.setButtonText (String::fromUTF8 ("Rápida"));
    sustain_.setTooltip (String::fromUTF8 ("Las notas se apagan despacio al soltarlas."));
    rotarySpeed_->setTooltip (String::fromUTF8 ("También con la rueda de modulación: arriba rápido, abajo lento."));

    volume_ = std::make_unique<Knob> (st, "organVolume", "NIVEL");
    click_ = std::make_unique<Knob> (st, "organClick", "CLICK");
    drive_ = std::make_unique<Knob> (st, "rotaryDrive", "DRIVE");
    for (Component* c : { (Component*) volume_.get(), (Component*) click_.get(), (Component*) drive_.get() }) all_.push_back (c);

    for (auto* c : all_) parent_.addChildComponent (c);
}

OrganPanel::~OrganPanel() = default;

void OrganPanel::setVisible (bool visible)
{
    for (auto* c : all_) c->setVisible (visible);
}

std::vector<OrganPanel::Group> OrganPanel::groups (int knobW)
{
    std::vector<Group> g;
    g.push_back ({ 9 * 30, [this] (Rectangle<int> a)
    {
        drawbarLabel_.setBounds (a.removeFromTop (16));
        a.removeFromTop (4);
        const int w = a.getWidth() / 9;
        for (auto* d : drawbars_) d->setBounds (a.removeFromLeft (w));
    } });
    g.push_back ({ 228, [this] (Rectangle<int> a)
    {
        modelLabel_.setBounds (a.removeFromTop (16));
        a.removeFromTop (6);
        model_->setBounds (a.removeFromTop (54));
        a.removeFromTop (8);
        auto vib = a.removeFromTop (24);
        vib_.setBounds (vib.removeFromLeft (128));
        sustain_.setBounds (vib);
        a.removeFromTop (4);
        vibType_->setBounds (a.removeFromTop (22));
    } });
    g.push_back ({ 120, [this] (Rectangle<int> a)
    {
        percLabel_.setBounds (a.removeFromTop (16));
        a.removeFromTop (6);
        for (auto* b : { &percOn_, &perc3rd_, &percFast_, &percSoft_ }) { b->setBounds (a.removeFromTop (24)); a.removeFromTop (3); }
    } });
    g.push_back ({ 132 + knobW, [this, knobW] (Rectangle<int> a)
    {
        rotaryLabel_.setBounds (a.removeFromTop (16).withWidth (132));
        drive_->setBounds (a.removeFromRight (knobW).withTrimmedTop (-16));
        a.removeFromTop (6);
        rotaryOn_.setBounds (a.removeFromTop (24));
        a.removeFromTop (8);
        rotarySpeed_->setBounds (a.removeFromTop (28).withTrimmedRight (8));
    } });
    g.push_back ({ 2 * knobW, [this, knobW] (Rectangle<int> a)
    {
        volume_->setBounds (a.removeFromLeft (knobW));
        click_->setBounds (a.removeFromLeft (knobW));
    } });
    return g;
}
