#include "PluginEditor.h"
#include "Autosampler.h"
#include "SfgrExport.h"
#include "SelfSampleExport.h"
#include "BinaryData.h"
#include "AboutWindow.h"

#include <map>

using namespace juce;

// ------------------------------------------------------------------------------------ estética

const Colour KeysLookAndFeel::bg { 0xff0b0b0e }, KeysLookAndFeel::panel { 0xff131317 }, KeysLookAndFeel::raised { 0xff1c1c21 },
             KeysLookAndFeel::stroke { 0x16ffffff }, KeysLookAndFeel::text { 0xffececee }, KeysLookAndFeel::dim { 0xff8f8f96 },
             KeysLookAndFeel::accent { 0xffffb547 }, KeysLookAndFeel::danger { 0xffff6b6b };

KeysLookAndFeel::KeysLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, bg);
    setColour (Label::textColourId, text);
    setColour (TextButton::buttonColourId, raised);
    setColour (TextButton::buttonOnColourId, accent);
    setColour (TextButton::textColourOffId, text);
    setColour (TextButton::textColourOnId, Colours::black);
    setColour (Slider::textBoxTextColourId, dim);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::rotarySliderFillColourId, accent);
    setColour (PopupMenu::backgroundColourId, raised);
    setColour (PopupMenu::textColourId, text);
    setColour (PopupMenu::headerTextColourId, accent);
    setColour (PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (PopupMenu::highlightedTextColourId, text);
    setColour (ToggleButton::textColourId, text);
    setColour (ToggleButton::tickColourId, accent);
}

void KeysLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto r = Rectangle<int> (x, y, w, h).toFloat().reduced (4);
    const float size = jmin (r.getWidth(), r.getHeight());
    const auto c = r.getCentre();
    const float radius = size * 0.5f, track = 3.0f;
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getMinimum() < 0 && approximatelyEqual (-s.getMinimum(), s.getMaximum());
    const float from = bipolar ? start + (float) s.valueToProportionOfLength (0) * (end - start) : start;

    Path bgArc;
    bgArc.addCentredArc (c.x, c.y, radius - track, radius - track, 0, start, end, true);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.strokePath (bgArc, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    Path valueArc;
    valueArc.addCentredArc (c.x, c.y, radius - track, radius - track, 0, jmin (from, angle), jmax (from, angle), true);
    g.setColour (s.isEnabled() ? accent : dim);
    g.strokePath (valueArc, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    const float knob = radius - track * 3;
    g.setColour (raised.brighter (0.15f));
    g.fillEllipse (c.x - knob, c.y - knob, knob * 2, knob * 2);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawEllipse (c.x - knob, c.y - knob, knob * 2, knob * 2, 1.0f);
    const Point<float> tip = c.getPointOnCircumference (knob * 0.8f, angle);
    g.setColour (text);
    g.drawLine ({ c.getPointOnCircumference (knob * 0.3f, angle), tip }, 2.2f);
}

void KeysLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent : raised.brighter (down ? 0.2f : over ? 0.1f : 0.0f));
    g.fillRoundedRectangle (r, 7.0f);
    if (! on)
    {
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }
}

void KeysLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const auto sw = Rectangle<float> (r.getX(), r.getCentreY() - 9, 32, 18);
    g.setColour (b.getToggleState() ? accent : raised.brighter (over ? 0.15f : 0.05f));
    g.fillRoundedRectangle (sw, 9);
    g.setColour (b.getToggleState() ? Colours::black.withAlpha (0.8f) : dim);
    const float kx = b.getToggleState() ? sw.getRight() - 16 : sw.getX() + 2;
    g.fillEllipse (kx, sw.getY() + 2, 14, 14);
    g.setColour (text);
    g.setFont (FontOptions (13.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (40), Justification::centredLeft);
}

Font KeysLookAndFeel::getTextButtonFont (TextButton&, int) { return FontOptions (13.0f, Font::bold); }

Label* KeysLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (11.5f));
    l->setColour (Label::textColourId, dim);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    return l;
}

// ------------------------------------------------------------------------------------ selector

void PianoDisplay::set (String name, String details, String error, bool loading, float progress, bool hasLibrary)
{
    empty_ = name.isEmpty();
    name_ = name; details_ = details; error_ = error;
    loading_ = loading; progress_ = progress; hasLibrary_ = hasLibrary;
    repaint();
}

Rectangle<float> PianoDisplay::arrow (bool left) const
{
    auto r = getLocalBounds().toFloat().reduced (8);
    return left ? r.removeFromLeft (36) : r.removeFromRight (36);
}

void PianoDisplay::paint (Graphics& g)
{
    using L = KeysLookAndFeel;
    const auto r = getLocalBounds().toFloat();
    g.setColour (L::panel);
    g.fillRoundedRectangle (r, 12);
    g.setColour (L::stroke);
    g.drawRoundedRectangle (r.reduced (0.5f), 12, 1);

    for (bool left : { true, false })
    {
        const auto a = arrow (left);
        g.setColour (hover_ == (left ? -1 : 1) ? L::text : L::dim);
        Path p;
        const auto c = a.getCentre();
        if (left) p.addTriangle (c.x + 5, c.y - 9, c.x + 5, c.y + 9, c.x - 6, c.y);
        else p.addTriangle (c.x - 5, c.y - 9, c.x - 5, c.y + 9, c.x + 6, c.y);
        g.fillPath (p);
    }

    auto text = r.reduced (56, 12);
    if (empty_ && ! loading_)
    {
        g.setColour (L::text);
        g.setFont (FontOptions (20.0f, Font::bold));
        g.drawText (hasLibrary_ ? "Elige un piano" : "Carga un piano .npno", text.removeFromTop (text.getHeight() * 0.5f), Justification::centredBottom);
        g.setColour (L::dim);
        g.setFont (FontOptions (13.0f));
        g.drawFittedText (hasLibrary_ ? "Haz clic para ver tu biblioteca"
                                      : "Abre un archivo, añade la carpeta de tu librería o muestrea un instrumento"_u8,
                          text.toNearestInt(), Justification::centredTop, 2);
    }
    else
    {
        g.setColour (L::text);
        g.setFont (FontOptions (24.0f, Font::bold));
        g.drawFittedText (empty_ ? String ("Cargando…"_u8) : name_, text.removeFromTop (text.getHeight() * 0.58f).toNearestInt(), Justification::centredBottom, 1);
        g.setColour (L::dim);
        g.setFont (FontOptions (13.0f));
        g.drawFittedText (details_, text.toNearestInt(), Justification::centredTop, 1);
    }

    if (loading_)
    {
        const auto bar = r.reduced (56, 0).removeFromBottom (10).withHeight (3);
        g.setColour (Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (L::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * jlimit (0.02f, 1.0f, progress_)), 1.5f);
    }
    else if (error_.isNotEmpty())
    {
        g.setColour (L::danger);
        g.setFont (FontOptions (12.0f));
        g.drawFittedText (error_, r.reduced (56, 4).removeFromBottom (18).toNearestInt(), Justification::centred, 1);
    }
}

void PianoDisplay::mouseUp (const MouseEvent& e)
{
    if (arrow (true).contains (e.position)) { if (onPrev) onPrev(); }
    else if (arrow (false).contains (e.position)) { if (onNext) onNext(); }
    else if (onClick) onClick();
}

void PianoDisplay::mouseMove (const MouseEvent& e)
{
    const int h = arrow (true).contains (e.position) ? -1 : arrow (false).contains (e.position) ? 1 : 0;
    if (h != hover_) { hover_ = h; repaint(); }
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void PianoDisplay::mouseExit (const MouseEvent&) { hover_ = 0; repaint(); }

// ------------------------------------------------------------------------------------ mando

Knob::Knob (AudioProcessorValueTreeState& state, const String& paramId, const String& text)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 16);
    slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxTextColourId, KeysLookAndFeel::dim);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    label.setText (text, dontSendNotification);
    label.setJustificationType (Justification::centred);
    label.setFont (FontOptions (12.0f, Font::bold));
    label.setColour (Label::textColourId, KeysLookAndFeel::text);
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    // Doble clic: valor por defecto.
    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

// ------------------------------------------------------------------------------------ pestañas

SectionTab::SectionTab (RangedAudioParameter& onParam, String text)
    : on_ (onParam, [this] (float v) { isOn_ = v > 0.5f; repaint(); }), text_ (std::move (text))
{
    on_.sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void SectionTab::paint (Graphics& g)
{
    using L = KeysLookAndFeel;
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (selected_ ? L::raised.brighter (0.12f) : L::panel);
    g.fillRoundedRectangle (r, 7);
    g.setColour (selected_ ? L::accent.withAlpha (0.7f) : L::stroke);
    g.drawRoundedRectangle (r, 7, 1);
    // Piloto: encendido en ámbar.
    const auto led = Rectangle<float> (r.getX() + 12, r.getCentreY() - 5, 10, 10);
    g.setColour (isOn_ ? L::accent : Colours::white.withAlpha (0.15f));
    g.fillEllipse (led);
    if (isOn_)
    {
        g.setColour (L::accent.withAlpha (0.25f));
        g.drawEllipse (led.expanded (3), 2);
    }
    g.setColour (selected_ ? L::text : L::dim);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText (text_, r.withTrimmedLeft (32), Justification::centredLeft);
}

void SectionTab::mouseUp (const MouseEvent& e)
{
    if (e.position.x < 30) on_.setValueAsCompleteGesture (isOn_ ? 0.0f : 1.0f); // el piloto
    else if (onSelect) onSelect();
}

// ------------------------------------------------------------------------------------ editor

KeysEditor::KeysEditor (KeysProcessor& p)
    : AudioProcessorEditor (p), proc_ (p),
      pianoTab_ (*p.params.getParameter ("pianoOn"), "PIANO"),
      organTab_ (*p.params.getParameter ("organOn"), String::fromUTF8 ("ÓRGANO")),
      samplesTab_ (*p.params.getParameter ("samplesOn"), "SAMPLES"),
      synthTab_ (*p.params.getParameter ("synthOn"), "SYNTH"),
      fmTab_ (*p.params.getParameter ("fmOn"), "FM"),
      volume_ (p.params, "volume", "VOLUMEN"), bass_ (p.params, "bass", "GRAVES"), treble_ (p.params, "treble", "AGUDOS"),
      reverb_ (p.params, "reverb", "REVERB"), room_ (p.params, "room", "SALA"), dynamics_ (p.params, "dynamics", "DINÁMICA"_u8),
      release_ (p.params, "release", "APAGADOR"), noise_ (p.params, "releaseNoise", "SOLTAR"), tune_ (p.params, "tune", "AFINACIÓN"_u8), pianoLevel_ (p.params, "pianoVolume", "NIVEL"),
      sampleLevel_ (p.params, "samplesVolume", "NIVEL"), sampleAttack_ (p.params, "samplesAttack", "ATAQUE"),
      sampleRelease_ (p.params, "samplesRelease", "LIBERACIÓN"_u8), sampleVelocity_ (p.params, "samplesVelocity", "VELOCIDAD"),
      keyboard_ (p.keyboardState, MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf_);

    for (auto* b : { &libraryButton_, &openButton_ }) addAndMakeVisible (b);
    libraryButton_.onClick = [this] { showLibraryMenu(); };
    openButton_.onClick = [this] { openFile(); };

    addAndMakeVisible (display_);
    for (auto* t : { &pianoTab_, &organTab_, &samplesTab_, &synthTab_, &fmTab_ }) addAndMakeVisible (t);
    pianoTab_.onSelect = [this] { setPanel (0); };
    organTab_.onSelect = [this] { setPanel (1); };
    samplesTab_.onSelect = [this] { setPanel (2); };
    synthTab_.onSelect = [this] { setPanel (3); };
    fmTab_.onSelect = [this] { setPanel (4); };
    fmTab_.setTooltip (String::fromUTF8 ("Piloto: enciende o apaga la sección FM (librerías .X7L/.X8L). Nombre: muestra sus controles."));
    synthTab_.setTooltip (String::fromUTF8 ("Piloto: enciende o apaga el synth (presets .ns4y). Nombre: muestra sus controles."));
    samplesTab_.setTooltip ("Piloto: enciende o apaga los samples (.nsmp3/.nsmp4 y librerías muestreadas con Keys). Nombre: muestra sus controles.");
    for (auto* k : { &sampleLevel_, &sampleAttack_, &sampleRelease_, &sampleVelocity_ }) addChildComponent (k);
    sampleOctaveLabel_.setText ("OCTAVA", dontSendNotification);
    sampleOctaveLabel_.setFont (FontOptions (12.0f, Font::bold));
    sampleOctaveLabel_.setJustificationType (Justification::centred);
    addChildComponent (sampleOctaveLabel_);
    sampleOctave_.setSliderStyle (Slider::IncDecButtons);
    sampleOctave_.setTextBoxStyle (Slider::TextBoxLeft, false, 44, 26);
    sampleOctave_.setIncDecButtonsMode (Slider::incDecButtonsNotDraggable);
    sampleOctave_.setColour (Slider::textBoxOutlineColourId, KeysLookAndFeel::stroke);
    sampleOctave_.setColour (Slider::textBoxTextColourId, KeysLookAndFeel::text);
    sampleOctave_.setColour (Slider::textBoxBackgroundColourId, KeysLookAndFeel::panel);
    addChildComponent (sampleOctave_);
    sampleOctaveAttachment_ = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.params, "samplesOctave", sampleOctave_);
    pianoTab_.setTooltip ("Piloto: enciende o apaga el piano. Nombre: muestra sus controles.");
    organTab_.setTooltip (String::fromUTF8 ("Piloto: enciende o apaga el órgano. Nombre: muestra sus controles."));
    organPanel_ = std::make_unique<OrganPanel> (*this, p);
    synthPanel_ = std::make_unique<SynthPanel> (*this, p);
    fmPanel_ = std::make_unique<FmPanel> (*this, p);
    display_.onClick = [this] { showPianoMenu(); };
    display_.onPrev = [this] { step (-1); };
    display_.onNext = [this] { step (1); };

    for (auto* k : { &volume_, &bass_, &treble_, &reverb_, &room_, &dynamics_, &release_, &noise_, &tune_, &pianoLevel_ }) addAndMakeVisible (k);

    for (auto* l : { &touchLabel_, &transposeLabel_ })
    {
        l->setFont (FontOptions (12.0f, Font::bold));
        l->setJustificationType (Justification::centred);
        addAndMakeVisible (l);
    }
    touchLabel_.setText ("TOQUE", dontSendNotification);
    transposeLabel_.setText ("TRANSPONER", dontSendNotification);

    auto* touch = p.params.getParameter ("touch");
    touchParam_ = std::make_unique<ParameterAttachment> (*touch, [this] (float v)
    {
        for (int i = 0; i < 3; ++i) touchButtons_[i].setToggleState (i == roundToInt (v), dontSendNotification);
    });
    for (int i = 0; i < 3; ++i)
    {
        touchButtons_[i].setClickingTogglesState (false);
        touchButtons_[i].onClick = [this, i] { setTouch (i); };
        addAndMakeVisible (touchButtons_[i]);
    }
    touchParam_->sendInitialUpdate();

    addAndMakeVisible (softRelease_);
    softAttachment_ = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "softRelease", softRelease_);
    addAndMakeVisible (stringRes_);
    stringResAttachment_ = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.params, "stringRes", stringRes_);
    stringRes_.setTooltip ("Con el pedal pisado usa las grabaciones hechas con el pedal pisado (pianos Med, Lrg y XL). Ocupa unas dos veces más memoria."_u8);
    softRelease_.setTooltip ("Apagador más lento, como el Soft Release del instrumento."_u8);
    noise_.slider.setTooltip ("Ruido del apagador al soltar la tecla, en dB. Al mínimo (Off) no suena."_u8);

    transpose_.setSliderStyle (Slider::IncDecButtons);
    transpose_.setTextBoxStyle (Slider::TextBoxLeft, false, 44, 26);
    transpose_.setIncDecButtonsMode (Slider::incDecButtonsNotDraggable);
    transpose_.setColour (Slider::textBoxOutlineColourId, KeysLookAndFeel::stroke);
    transpose_.setColour (Slider::textBoxTextColourId, KeysLookAndFeel::text);
    transpose_.setColour (Slider::textBoxBackgroundColourId, KeysLookAndFeel::panel);
    addAndMakeVisible (transpose_);
    transposeAttachment_ = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.params, "transpose", transpose_);

    keyboard_.setAvailableRange (21, 108);
    keyboard_.setOctaveForMiddleC (4);
    keyboard_.setColour (MidiKeyboardComponent::whiteNoteColourId, Colour (0xffeeeef0));
    keyboard_.setColour (MidiKeyboardComponent::blackNoteColourId, Colour (0xff141417));
    keyboard_.setColour (MidiKeyboardComponent::keySeparatorLineColourId, Colour (0x30000000));
    keyboard_.setColour (MidiKeyboardComponent::keyDownOverlayColourId, KeysLookAndFeel::accent.withAlpha (0.85f));
    keyboard_.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, KeysLookAndFeel::accent.withAlpha (0.25f));
    keyboard_.setColour (MidiKeyboardComponent::shadowColourId, Colour (0x40000000));
    keyboard_.setColour (MidiKeyboardComponent::upDownButtonBackgroundColourId, KeysLookAndFeel::raised);
    keyboard_.setColour (MidiKeyboardComponent::upDownButtonArrowColourId, KeysLookAndFeel::dim);
    keyboard_.setVelocity (0.75f, true);
    addAndMakeVisible (keyboard_);

    proc_.pianoChanged.addChangeListener (this);
    proc_.library.addChangeListener (this);

    setResizable (true, true);
    // El synth necesita tres filas de mandos.
    setResizeLimits (980, 680, 1800, 1000);
    setSize (1100, 760);
    const bool organOnly = p.params.getRawParameterValue ("organOn")->load() > 0.5f && p.params.getRawParameterValue ("pianoOn")->load() < 0.5f;
    // Desarrollo: BK_PANEL=organ abre en el órgano (para capturas sin hacer clic).
    const bool devOrgan = SystemStats::getEnvironmentVariable ("BK_PANEL", {}) == "organ";
    // Desarrollo: BK_ORGAN_PRESET=<ruta .ns4o> lo abre al arrancar.
    if (const File dev (SystemStats::getEnvironmentVariable ("BK_ORGAN_PRESET", {})); dev.existsAsFile()) p.loadOrganPreset (dev);
    // Desarrollo: BK_SAMPLE=<ruta .nsmp4> lo abre y muestra la pestaña SAMPLES.
    if (const File dev (SystemStats::getEnvironmentVariable ("BK_SAMPLE", {})); dev.existsAsFile())
    {
        p.loadSample (dev);
        Timer::callAfterDelay (500, [safe = Component::SafePointer<KeysEditor> (this)] { if (safe != nullptr) safe->setPanel (2); });
    }
    // Desarrollo: BK_PIANO_PRESET=<ruta .ns4n>, cuando la biblioteca ya ha buscado.
    if (const File dev (SystemStats::getEnvironmentVariable ("BK_PIANO_PRESET", {})); dev.existsAsFile())
        Timer::callAfterDelay (3000, [&p, dev, safe = Component::SafePointer<KeysEditor> (this)]
        {
            p.loadPianoPreset (dev);
            if (safe != nullptr) safe->setPanel (0);
        });
    // Desarrollo: BK_SYNTH_PRESET=<ruta .ns4y>, cuando la biblioteca ya ha buscado (por su sample).
    if (const File dev (SystemStats::getEnvironmentVariable ("BK_SYNTH_PRESET", {})); dev.existsAsFile())
        Timer::callAfterDelay (3000, [&p, dev, safe = Component::SafePointer<KeysEditor> (this)]
        {
            p.loadSynthPreset (dev);
            if (safe != nullptr) safe->setPanel (3);
        });
    // Desarrollo: BK_SAMPLER=1 abre el muestreador.
    if (SystemStats::getEnvironmentVariable ("BK_SAMPLER", {}) == "1" && AutosamplerWindow::isAvailable())
        Timer::callAfterDelay (800, [safe = Component::SafePointer<KeysEditor> (this)] { if (safe != nullptr) safe->openAutosampler(); });
    // Desarrollo: BK_FM=<ruta .X7L>[#índice] la abre en la sección FM.
    if (const auto dev = SystemStats::getEnvironmentVariable ("BK_FM", {}); dev.isNotEmpty())
    {
        const File f (dev.upToFirstOccurrenceOf ("#", false, false));
        const int index = dev.contains ("#") ? dev.fromFirstOccurrenceOf ("#", false, false).getIntValue() : -1;
        if (f.existsAsFile())
            Timer::callAfterDelay (1000, [&p, f, index, safe = Component::SafePointer<KeysEditor> (this)]
            {
                p.loadFmPerformance (f, index);
                if (safe != nullptr) safe->setPanel (4);
            });
    }
    const bool pianoOff = p.params.getRawParameterValue ("pianoOn")->load() < 0.5f;
    const bool synthOnly = p.params.getRawParameterValue ("synthOn")->load() > 0.5f && pianoOff && ! organOnly;
    const bool fmOnly = p.params.getRawParameterValue ("fmOn")->load() > 0.5f && pianoOff && ! organOnly && ! synthOnly;
    setPanel (organOnly || devOrgan ? 1 : synthOnly ? 3 : fmOnly ? 4 : 0);
    refresh();
    startTimerHz (15);
}

KeysEditor::~KeysEditor()
{
    if (sampler_ != nullptr) delete sampler_.getComponent();
    proc_.pianoChanged.removeChangeListener (this);
    proc_.library.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void KeysEditor::setTouch (int index)
{
    touchParam_->setValueAsCompleteGesture ((float) index);
}

void KeysEditor::paint (Graphics& g)
{
    using L = KeysLookAndFeel;
    g.fillAll (L::bg);
    const auto area = getLocalBounds();
    auto r = area;
    auto header = r.removeFromTop (52).reduced (20, 0);
    g.setFont (FontOptions (15.0f, Font::bold));
    g.setColour (L::text);
    if (showBrand_)
    {
        if (! brandLogo_.isValid())
            brandLogo_ = ImageCache::getFromMemory (BinaryData::headerlogo_png, BinaryData::headerlogo_pngSize);
        // El logo de la marca, a 30 px de alto, centrado en la cabecera.
        const float h = 30.0f, w = h * (float) brandLogo_.getWidth() / (float) jmax (1, brandLogo_.getHeight());
        g.drawImage (brandLogo_, Rectangle<float> ((float) header.getX(), header.getCentreY() - h / 2, w, h), RectanglePlacement::centred);
    }

    // Separadores de los grupos de mandos.
    g.setColour (L::stroke);
    g.drawHorizontalLine (area.getY() + 52, 0, (float) getWidth());

    g.setFont (FontOptions (11.0f));
    g.setColour (L::dim.withAlpha (0.8f));
    auto foot = area.withTrimmedTop (area.getHeight() - 22).reduced (20, 0);
    g.drawText (status_, foot, Justification::centredLeft);
    g.drawText ("BeTracks Keys no incluye sonidos: abre las librerías que ya tienes o muestrea tus instrumentos."_u8, foot, Justification::centredRight);
}

void KeysEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (52).reduced (20, 11);
    openButton_.setBounds (header.removeFromRight (90));
    header.removeFromRight (8);
    libraryButton_.setBounds (header.removeFromRight (110));
    // La marca solo si caben las cinco pestañas (en una ventana estrecha, no).
    showBrand_ = header.getWidth() >= 170 + 4 * 112 + 72 + 4 * 8;
    if (showBrand_) header.removeFromLeft (170);
    const int tabW = jlimit (84, 120, (header.getWidth() - 4 * 8 - 72) / 4);
    for (auto* t : { &pianoTab_, &organTab_, &samplesTab_, &synthTab_, &fmTab_ })
    {
        t->setBounds (header.removeFromLeft (t == &fmTab_ ? jmin (tabW, 72) : tabW));
        header.removeFromLeft (8);
    }
    r.removeFromBottom (22);

    // Mandos en grupos que pasan a otra fila si no caben: el sonido general siempre, y luego los de la
    // sección que se muestra (piano u órgano).
    const int availW = getWidth() - 40;
    const int knobW = jlimit (64, 92, availW / 14);
    constexpr int gap = 24, rowH = 132, displayH = 96;
    std::vector<OrganPanel::Group> groups;
    groups.push_back ({ 5 * knobW, [this, knobW] (Rectangle<int> a)
        { for (auto* k : { &volume_, &bass_, &treble_, &reverb_, &room_ }) k->setBounds (a.removeFromLeft (knobW)); } });
    if (panel_ == 0)
    {
        groups.push_back ({ 180, [this] (Rectangle<int> a)
        {
            touchLabel_.setBounds (a.removeFromTop (16));
            a.removeFromTop (8);
            auto tb = a.removeFromTop (28);
            for (auto& b : touchButtons_) b.setBounds (tb.removeFromLeft (60).reduced (2, 0));
            a.removeFromTop (10);
            softRelease_.setBounds (a.removeFromTop (24));
            a.removeFromTop (4);
            stringRes_.setBounds (a.removeFromTop (24));
        } });
        groups.push_back ({ 4 * knobW, [this, knobW] (Rectangle<int> a)
            { for (auto* k : { &pianoLevel_, &dynamics_, &release_, &noise_ }) k->setBounds (a.removeFromLeft (knobW)); } });
        groups.push_back ({ knobW + 8 + 112, [this, knobW] (Rectangle<int> a)
        {
            tune_.setBounds (a.removeFromLeft (knobW));
            a.removeFromLeft (8);
            transposeLabel_.setBounds (a.removeFromTop (16));
            a.removeFromTop (8);
            transpose_.setBounds (a.removeFromTop (28));
        } });
    }
    else if (panel_ == 1)
        for (auto& g : organPanel_->groups (knobW)) groups.push_back (g);
    else if (panel_ == 3)
        for (auto& g : synthPanel_->groups (knobW)) groups.push_back (g);
    else if (panel_ == 4)
        for (auto& g : fmPanel_->groups (knobW)) groups.push_back (g);
    else
    {
        groups.push_back ({ 4 * knobW, [this, knobW] (Rectangle<int> a)
            { for (auto* k : { &sampleLevel_, &sampleAttack_, &sampleRelease_, &sampleVelocity_ }) k->setBounds (a.removeFromLeft (knobW)); } });
        groups.push_back ({ 112, [this] (Rectangle<int> a)
        {
            sampleOctaveLabel_.setBounds (a.removeFromTop (16));
            a.removeFromTop (8);
            sampleOctave_.setBounds (a.removeFromTop (28));
        } });
    }

    std::vector<int> rowOf (groups.size());
    int rows = 1, x = 0;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        const int need = (x > 0 ? gap : 0) + groups[g].width;
        if (x > 0 && x + need > availW) { ++rows; x = groups[g].width; }
        else x += need;
        rowOf[g] = rows - 1;
    }
    const int controlsH = rows * rowH + (rows - 1) * 8;

    {
        // Teclado: una franja con lo que dejen los mandos, entre 80 y 150 px.
        const int kbH = jlimit (80, 150, r.getHeight() - (12 + displayH + 12 + controlsH + 16));
        keyboard_.setBounds (r.removeFromBottom (kbH).reduced (20, 0));
        keyboard_.setKeyWidth ((float) keyboard_.getWidth() / 52.0f);
    }
    r.removeFromBottom (10);

    auto top = r.reduced (20, 12);
    display_.setBounds (top.removeFromTop (jmin (displayH, top.getHeight() / 3)));
    top.removeFromTop (12);

    std::vector<Rectangle<int>> rowArea;
    for (int i = 0; i < rows; ++i)
    {
        rowArea.push_back (top.removeFromTop (rowH));
        top.removeFromTop (8);
    }
    int lastRow = -1;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        auto& row = rowArea[size_t (rowOf[g])];
        if (rowOf[g] == lastRow) row.removeFromLeft (gap);
        lastRow = rowOf[g];
        groups[g].place (row.removeFromLeft (groups[g].width));
    }
}

void KeysEditor::setPanel (int panel)
{
    panel_ = panel;
    pianoTab_.setSelected (panel == 0);
    organTab_.setSelected (panel == 1);
    samplesTab_.setSelected (panel == 2);
    synthTab_.setSelected (panel == 3);
    fmTab_.setSelected (panel == 4);
    for (Component* c : { (Component*) &sampleLevel_, (Component*) &sampleAttack_, (Component*) &sampleRelease_,
                          (Component*) &sampleVelocity_, (Component*) &sampleOctaveLabel_, (Component*) &sampleOctave_ })
        c->setVisible (panel == 2);
    for (Component* c : { (Component*) &touchLabel_, (Component*) &touchButtons_[0], (Component*) &touchButtons_[1], (Component*) &touchButtons_[2],
                          (Component*) &softRelease_, (Component*) &stringRes_, (Component*) &dynamics_, (Component*) &release_,
                          (Component*) &noise_, (Component*) &tune_, (Component*) &pianoLevel_, (Component*) &transposeLabel_, (Component*) &transpose_ })
        c->setVisible (panel == 0);
    organPanel_->setVisible (panel == 1);
    synthPanel_->setVisible (panel == 3);
    fmPanel_->setVisible (panel == 4);
    resized();
    refresh();
    repaint();
}

void KeysEditor::refresh()
{
    if (panel_ == 4)
    {
        fmPanel_->update();
        display_.set (proc_.currentFmName(), proc_.currentFmDetails(), proc_.lastError(), false, 0,
                      ! proc_.library.fmEntries().empty() || proc_.fmLibrary() != nullptr);
        return;
    }
    if (panel_ == 3)
    {
        synthPanel_->update();
        display_.set (proc_.currentSynthName(), proc_.currentSynthDetails(), proc_.lastError(), false, 0,
                      ! proc_.library.synthEntries().empty());
        return;
    }
    if (panel_ == 2)
    {
        display_.set (proc_.currentSampleName(), proc_.isLoadingSample() ? String() : proc_.currentSampleDetails(), proc_.lastError(),
                      proc_.isLoadingSample(), 0.5f, ! proc_.library.sampleEntries().empty());
        return;
    }
    if (panel_ == 1)
    {
        display_.set (proc_.currentOrganName(), proc_.currentOrganDetails(), proc_.lastError(), false, 0,
                      ! proc_.library.organEntries().empty());
        return;
    }
    const bool hasLibrary = ! proc_.library.entries().empty();
    const bool busy = proc_.isLoading() || proc_.isImporting();
    display_.set (proc_.currentPianoName(),
                  proc_.isImporting() ? String ("Importando…"_u8) : proc_.isLoading() ? String ("Cargando…"_u8) : proc_.currentPianoDetails(),
                  proc_.lastError(), busy, proc_.isImporting() ? 0.5f : proc_.loadProgress(), hasLibrary);
}

void KeysEditor::changeListenerCallback (ChangeBroadcaster*) { refresh(); repaint(); }

void KeysEditor::timerCallback()
{
    if (const int want = proc_.takePanelRequest(); want >= 0 && want != panel_) setPanel (want);
    if (proc_.isLoading() || proc_.isImporting() || proc_.isLoadingSample() || panel_ == 1 || panel_ == 3 || panel_ == 4) refresh();
    const auto s = String (proc_.activeVoices()) + " voces · "_u8 + String (proc_.getSampleRate() / 1000.0, 1) + " kHz"
                 + (proc_.library.isScanning() ? " · buscando pianos…"_u8 : "");
    if (s != status_) { status_ = s; repaint(); }
}

void KeysEditor::showOrganMenu()
{
    const auto list = proc_.library.organEntries();
    PopupMenu menu;
    String group;
    const auto current = proc_.currentOrganPreset();
    // El órgano tal como suena ahora puede ir a BeTracks Pro (Keys lo toca nota a nota y lo guarda en .sfgr).
    menu.addItem (100000, String::fromUTF8 ("Exportar este órgano a BeTracks Pro (.sfgr)…"));
    menu.addSeparator();
    if (list.empty())
        menu.addItem (-1, String::fromUTF8 ("No hay presets de órgano (.ns4o): añade su carpeta en Biblioteca"), false);
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].group != group) { group = list[i].group; menu.addSectionHeader (group); }
        menu.addItem (int (i) + 1, list[i].name, true, list[i].file == current);
    }
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (display_).withMaximumNumColumns (4), [this, list] (int r)
    {
        if (r == 100000) { bk::selfsample::exportToBeTracks (proc_, bk::selfsample::Source::Organ); return; }
        if (r > 0) proc_.loadOrganPreset (list[size_t (r - 1)].file);
    });
}

void KeysEditor::showSampleMenu()
{
    const auto list = proc_.library.sampleEntries();
    const auto loaded = proc_.currentSampleFile();
    if (list.empty() && ! loaded.existsAsFile())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Samples",
            String::fromUTF8 ("No hay samples (.nsmp3/.nsmp4 o librerías .bkm) en la biblioteca. Añade la carpeta que los contiene, "
                              "o crea los tuyos con Biblioteca › Muestrear un instrumento externo."));
        return;
    }
    PopupMenu menu;
    std::map<String, PopupMenu> groups;
    StringArray order;
    const auto current = proc_.currentSampleFile();
    // El sample que suena puede ir a BeTracks Pro (formato .sfgr).
    if (loaded.existsAsFile() && ! proc_.isLoadingSample())
    {
        menu.addItem (100000, String::fromUTF8 ("Exportar «") + proc_.currentSampleName() + String::fromUTF8 ("» a BeTracks Pro (.sfgr)…"));
        menu.addSeparator();
    }
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (! groups.count (list[i].group)) order.add (list[i].group);
        groups[list[i].group].addItem (int (i) + 1, list[i].name + "  ·  " + formatMB (double (list[i].bytes)), true, list[i].file == current);
    }
    for (const auto& g : order) menu.addSubMenu (g, groups[g], true, nullptr, current.getParentDirectory().getFileName().replaceCharacter ('_', ' ') == g);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (display_), [this, list] (int r)
    {
        if (r == 100000)
        {
            bk::sfgrui::exportToBeTracks (proc_.currentSampleFile(), proc_.params.getRawParameterValue ("samplesAttack")->load(),
                                         proc_.params.getRawParameterValue ("samplesRelease")->load());
            return;
        }
        if (r > 0) proc_.loadSample (list[size_t (r - 1)].file);
    });
}

void KeysEditor::showSynthMenu()
{
    const auto list = proc_.library.synthEntries();
    PopupMenu menu;
    // El synth tal como suena ahora puede ir a BeTracks Pro (Keys lo toca nota a nota y lo guarda en .sfgr).
    menu.addItem (100000, String::fromUTF8 ("Exportar este synth a BeTracks Pro (.sfgr)…"));
    menu.addSeparator();
    if (list.empty())
        menu.addItem (-1, String::fromUTF8 ("No hay presets de synth (.ns4y): añade su carpeta en Biblioteca"), false);
    std::map<String, PopupMenu> groups;
    StringArray order;
    const auto current = proc_.currentSynthPreset();
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (! groups.count (list[i].group)) order.add (list[i].group);
        groups[list[i].group].addItem (int (i) + 1, list[i].name + "  ·  " + list[i].variant, true, list[i].file == current);
    }
    if (order.size() == 1)
    {
        // Un solo banco: sus presets directamente, debajo de la exportación.
        for (size_t i = 0; i < list.size(); ++i)
            menu.addItem (int (i) + 1, list[i].name + "  ·  " + list[i].variant, true, list[i].file == current);
    }
    else for (const auto& g : order) menu.addSubMenu (g, groups[g], true, nullptr, current.getParentDirectory().getFileName() == g);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (display_).withMaximumNumColumns (2), [this, list] (int r)
    {
        if (r == 100000) { bk::selfsample::exportToBeTracks (proc_, bk::selfsample::Source::Synth); return; }
        if (r > 0) proc_.loadSynthPreset (list[size_t (r - 1)].file);
    });
}

void KeysEditor::showFmMenu()
{
    auto list = proc_.library.fmEntries();
    const auto current = proc_.currentFmFile();
    // La librería abierta con «Abrir…» aunque su carpeta aún no esté escaneada.
    if (const auto* lib = proc_.fmLibrary(); lib != nullptr
        && std::none_of (list.begin(), list.end(), [&] (const PianoLibrary::Entry& e) { return e.file == current; }))
    {
        PianoLibrary::Entry e;
        e.file = current;
        e.name = current.getFileNameWithoutExtension();
        for (int i : proc_.fmPlayable()) e.performances.push_back ({ i, String::fromUTF8 (lib->performances[size_t (i)].name.c_str()) });
        list.insert (list.begin(), e);
    }
    if (list.empty())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "FM",
            String::fromUTF8 ("No hay librerías FM (.X7L, .X7U, .X8L, .X8U) en la biblioteca. Ábrela con «Abrir…» "
                              "o añade su carpeta en Biblioteca. Keys toca sus partes FM y las de muestras cuyas ondas vienen en la librería."));
        return;
    }
    // Ids: archivo × 10000 + índice de la performance + 1.
    auto addPerformances = [&] (PopupMenu& m, size_t f)
    {
        const auto& e = list[f];
        const auto* lib = e.file == current ? proc_.fmLibrary() : nullptr;
        for (const auto& [index, name] : e.performances)
        {
            String label = name;
            if (lib != nullptr && ! lib->performances[size_t (index)].complete()) label << String::fromUTF8 ("  · parcial");
            m.addItem (int (f) * 10000 + index + 1, label, true, e.file == current && index == proc_.currentFmIndex());
        }
    };
    PopupMenu menu;
    // La performance que suena puede ir a BeTracks Pro (Keys la toca nota a nota). Id fuera del rango archivo × 10000.
    constexpr int kExportId = 99999999;
    if (proc_.currentFmIndex() >= 0 && proc_.currentFmFile().existsAsFile())
    {
        menu.addItem (kExportId, String::fromUTF8 ("Exportar esta performance a BeTracks Pro (.sfgr)…"));
        menu.addSeparator();
    }
    if (list.size() == 1) addPerformances (menu, 0);
    else
        for (size_t f = 0; f < list.size(); ++f)
        {
            PopupMenu sub;
            addPerformances (sub, f);
            menu.addSubMenu (list[f].name + "  (" + String (list[f].performances.size()) + ")", sub, true, nullptr, list[f].file == current);
        }
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (display_).withMaximumNumColumns (4), [this, list] (int r)
    {
        if (r <= 0) return;
        if (r == 99999999) { bk::selfsample::exportToBeTracks (proc_, bk::selfsample::Source::Fm); return; }
        const auto f = size_t ((r - 1) / 10000);
        if (f < list.size()) proc_.loadFmPerformance (list[f].file, (r - 1) % 10000);
    });
}

void KeysEditor::showPianoMenu()
{
    if (panel_ == 4) { showFmMenu(); return; }
    if (panel_ == 1) { showOrganMenu(); return; }
    if (panel_ == 3) { showSynthMenu(); return; }
    if (panel_ == 2) { showSampleMenu(); return; }
    const auto list = proc_.library.entries();
    const auto presets = proc_.library.pianoPresetEntries();
    if (list.empty() && presets.empty()) { showLibraryMenu(); return; }
    PopupMenu menu;
    String group;
    const auto current = proc_.currentPianoFile();
    // El piano que suena puede ir a BeTracks Pro con la caída de cada nota (Source/core/PianoExport).
    if (current.existsAsFile())
    {
        menu.addItem (200000, String::fromUTF8 ("Exportar este piano a BeTracks Pro (.sfgr)…"));
        menu.addSeparator();
    }
    if (! presets.empty())
    {
        // Presets del Stage 4 (.ns4n): cada uno elige su piano y sus ajustes. Ids desde 10000.
        PopupMenu sub;
        String g;
        const auto currentPreset = proc_.currentPianoPreset();
        for (size_t i = 0; i < presets.size(); ++i)
        {
            if (presets[i].group != g) { g = presets[i].group; sub.addSectionHeader (g); }
            sub.addItem (10000 + int (i), presets[i].name + "  ·  " + presets[i].variant, true, presets[i].file == currentPreset);
        }
        menu.addSubMenu ("Presets de piano (" + String (presets.size()) + ")", sub);
        menu.addSeparator();
    }
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].group != group) { group = list[i].group; menu.addSectionHeader (group); }
        menu.addItem (int (i) + 1, list[i].label() + "  ·  "_u8 + formatMB (double (list[i].bytes)), true, list[i].file == current);
    }
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (display_).withMaximumNumColumns (3), [this, list, presets] (int r)
    {
        if (r == 200000) { bk::sfgrui::exportPianoToBeTracks (proc_.currentPianoFile()); return; }
        if (r >= 10000) proc_.loadPianoPreset (presets[size_t (r - 10000)].file);
        else if (r > 0) proc_.loadPiano (list[size_t (r - 1)].file);
    });
}

void KeysEditor::step (int delta)
{
    if (panel_ == 4)
    {
        const auto& list = proc_.fmPlayable();
        if (list.empty()) { showFmMenu(); return; }
        const auto it = std::find (list.begin(), list.end(), proc_.currentFmIndex());
        const int n = int (list.size());
        const int at = it == list.end() ? -1 : int (it - list.begin());
        proc_.loadFmPerformance (proc_.currentFmFile(), list[size_t (at < 0 ? (delta > 0 ? 0 : n - 1) : (at + delta + n) % n)]);
        return;
    }
    if (panel_ == 3)
    {
        const auto list = proc_.library.synthEntries();
        if (list.empty()) { showSynthMenu(); return; }
        int at = -1;
        for (size_t i = 0; i < list.size(); ++i) if (list[i].file == proc_.currentSynthPreset()) at = int (i);
        const int n = int (list.size());
        proc_.loadSynthPreset (list[size_t (at < 0 ? (delta > 0 ? 0 : n - 1) : (at + delta + n) % n)].file);
        return;
    }
    if (panel_ == 2)
    {
        const auto list = proc_.library.sampleEntries();
        if (list.empty()) { showSampleMenu(); return; }
        int at = -1;
        for (size_t i = 0; i < list.size(); ++i) if (list[i].file == proc_.currentSampleFile()) at = int (i);
        const int n = int (list.size());
        proc_.loadSample (list[size_t (at < 0 ? (delta > 0 ? 0 : n - 1) : (at + delta + n) % n)].file);
        return;
    }
    if (panel_ == 1)
    {
        const auto list = proc_.library.organEntries();
        if (list.empty()) { showOrganMenu(); return; }
        int at = -1;
        for (size_t i = 0; i < list.size(); ++i) if (list[i].file == proc_.currentOrganPreset()) at = int (i);
        const int n = int (list.size());
        proc_.loadOrganPreset (list[size_t (at < 0 ? (delta > 0 ? 0 : n - 1) : (at + delta + n) % n)].file);
        return;
    }
    if (proc_.currentPianoPreset() != File())
    {
        const auto presets = proc_.library.pianoPresetEntries();
        int at = -1;
        for (size_t i = 0; i < presets.size(); ++i) if (presets[i].file == proc_.currentPianoPreset()) at = int (i);
        if (! presets.empty())
        {
            const int n = int (presets.size());
            proc_.loadPianoPreset (presets[size_t (at < 0 ? 0 : (at + delta + n) % n)].file);
            return;
        }
    }
    const auto list = proc_.library.entries();
    if (list.empty()) { showLibraryMenu(); return; } // nada que recorrer: que se vea por qué
    const auto current = proc_.currentPianoFile();
    int at = -1;
    for (size_t i = 0; i < list.size(); ++i) if (list[i].file == current) at = int (i);
    const int n = int (list.size());
    const int next = at < 0 ? (delta > 0 ? 0 : n - 1) : (at + delta + n) % n;
    proc_.loadPiano (list[size_t (next)].file);
}

void KeysEditor::showLibraryMenu()
{
    PopupMenu menu, remove;
    const auto folders = proc_.library.folders();
    menu.addSectionHeader ("Carpetas de pianos");
    if (folders.isEmpty()) menu.addItem (-1, "Ninguna todavía"_u8, false);
    for (int i = 0; i < folders.size(); ++i)
    {
        menu.addItem (100 + i, File (folders[i]).getFileName() + "   (" + folders[i] + ")", true, false);
        remove.addItem (200 + i, folders[i]);
    }
    menu.addSeparator();
    menu.addItem (1, "Añadir carpeta…"_u8);
    if (folders.size()) menu.addSubMenu ("Quitar carpeta", remove);
    menu.addItem (2, "Volver a buscar");
    menu.addSeparator();
    if (AutosamplerWindow::isAvailable())
        menu.addItem (4, "Muestrear un instrumento externo…"_u8);
    menu.addSeparator();
    menu.addItem (5, "Acerca de BeTracks Keys (créditos y licencias)…"_u8);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (libraryButton_), [this, folders] (int r)
    {
        if (r == 4) { openAutosampler(); return; }
        if (r == 5) { bk::about::show(); return; }
        if (r == 1)
        {
            chooser_ = std::make_unique<FileChooser> ("Carpeta con pianos .npno", File::getSpecialLocation (File::userDocumentsDirectory));
            chooser_->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& fc)
            {
                if (fc.getResult().isDirectory()) proc_.library.addFolder (fc.getResult());
            });
        }
        else if (r == 2) proc_.library.rescan();
        else if (r >= 100 && r < 200) File (folders[r - 100]).revealToUser();
        else if (r >= 200) proc_.library.removeFolder (folders[r - 200]);
    });
}

void KeysEditor::openAutosampler()
{
    if (sampler_ != nullptr) { sampler_->toFront (true); return; }
    sampler_ = new AutosamplerWindow ([&proc = proc_] (const File& f)
    {
        // La librería nueva entra en la biblioteca y suena en SAMPLES.
        proc.library.ensureFolder (f.getParentDirectory());
        proc.library.rescan();
        proc.loadAny (f);
    });
}


void KeysEditor::openFile()
{
    chooser_ = std::make_unique<FileChooser> (String::fromUTF8 ("Abrir un piano, un preset (piano, órgano o synth), un sample o una librería FM"),
                                              File::getSpecialLocation (File::userDocumentsDirectory), PianoLibrary::kPatterns);
    chooser_->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        proc_.library.ensureFolder (f.getParentDirectory());
        setPanel (proc_.loadAny (f));
    });
}
