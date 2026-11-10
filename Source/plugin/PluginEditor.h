// BeTracks Keys — interfaz.
#pragma once

#include "PluginProcessor.h"
#include "OrganPanel.h"
#include "SynthPanel.h"
#include "FmPanel.h"

class KeysLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static const juce::Colour bg, panel, raised, stroke, text, dim, accent, danger;
    KeysLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

/** Selector del piano: nombre, detalles, flechas y progreso de carga. */
class PianoDisplay : public juce::Component
{
public:
    std::function<void()> onClick, onPrev, onNext;
    void set (juce::String name, juce::String details, juce::String error, bool loading, float progress, bool hasLibrary);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> arrow (bool left) const;
    juce::String name_, details_, error_;
    bool loading_ = false, hasLibrary_ = false, empty_ = true;
    float progress_ = 0;
    int hover_ = 0; // -1 izquierda, 1 derecha
};

/** Un mando con su nombre debajo. */
struct Knob : juce::Component
{
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label);
    void resized() override;
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Pestaña de sección (PIANO / ÓRGANO): el piloto la enciende o apaga; el nombre la muestra. */
class SectionTab : public juce::Component, public juce::SettableTooltipClient
{
public:
    SectionTab (juce::RangedAudioParameter& onParam, juce::String text);
    std::function<void()> onSelect;
    void setSelected (bool s) { selected_ = s; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::ParameterAttachment on_;
    juce::String text_;
    bool isOn_ = false, selected_ = false;
};

class KeysEditor : public juce::AudioProcessorEditor, private juce::ChangeListener, private juce::Timer
{
public:
    explicit KeysEditor (KeysProcessor&);
    ~KeysEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    void showPianoMenu();
    void showOrganMenu();
    void showSampleMenu();
    void showSynthMenu();
    void showFmMenu();
    void openAutosampler();
    void setPanel (int panel);
    void showLibraryMenu();
    void openFile();
    void step (int delta);
    void setTouch (int index);

    KeysProcessor& proc_;
    KeysLookAndFeel lnf_;

    juce::TextButton libraryButton_ { "Biblioteca" }, openButton_ { "Abrir…"_u8 };
    PianoDisplay display_;
    SectionTab pianoTab_, organTab_, samplesTab_, synthTab_, fmTab_;
    int panel_ = 0; ///< 0 piano, 1 órgano, 2 samples, 3 synth, 4 FM
    bool showBrand_ = true;
    juce::Image brandLogo_;   ///< logo de la marca (BeTracks + EC + keys), de assets/brand
    Knob volume_, bass_, treble_, reverb_, room_, dynamics_, release_, noise_, tune_, pianoLevel_;
    Knob sampleLevel_, sampleAttack_, sampleRelease_, sampleVelocity_;
    juce::Label sampleOctaveLabel_;
    juce::Slider sampleOctave_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleOctaveAttachment_;
    juce::Label touchLabel_, transposeLabel_;
    juce::TextButton touchButtons_[3] { juce::TextButton ("Ligero"), juce::TextButton ("Normal"), juce::TextButton ("Duro") };
    juce::ToggleButton softRelease_ { "Soft Release" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> softAttachment_;
    juce::ToggleButton stringRes_ { "String Res" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stringResAttachment_;
    juce::Slider transpose_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transposeAttachment_;
    juce::MidiKeyboardComponent keyboard_;
    juce::String status_;
    juce::TooltipWindow tooltips_ { this, 500 };
    std::unique_ptr<OrganPanel> organPanel_;
    std::unique_ptr<SynthPanel> synthPanel_;
    std::unique_ptr<FmPanel> fmPanel_;
    juce::Component::SafePointer<juce::DocumentWindow> sampler_;   // muestreador (se borra solo al cerrarse)
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::ParameterAttachment> touchParam_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeysEditor)
};
