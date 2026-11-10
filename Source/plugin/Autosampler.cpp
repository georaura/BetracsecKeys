#include "Autosampler.h"
#include "AutosampleDsp.h"
#include "KeysSampleFile.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
const char* const kNotes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
String noteName (int n) { return String (kNotes[n % 12]) + String (n / 12 - 1); }

std::vector<int> velocityLayers (int count)
{
    switch (count)
    {
        case 1: return { 100 };
        case 2: return { 64, 120 };
        case 3: return { 45, 85, 120 };
        case 4: return { 32, 64, 96, 127 };
        case 5: return { 28, 52, 76, 100, 124 };
        case 6: return { 24, 44, 64, 84, 104, 124 };
        case 7: return { 20, 38, 56, 74, 92, 110, 127 };
        default: return { 16, 32, 48, 64, 80, 96, 112, 127 };
    }
}

/** "1-8, 12" → { 1..8, 12 } (programas 1..128). */
std::vector<int> parsePrograms (const String& text)
{
    std::vector<int> out;
    for (auto token : StringArray::fromTokens (text, ",; ", ""))
    {
        token = token.trim();
        if (token.isEmpty()) continue;
        const int a = token.upToFirstOccurrenceOf ("-", false, false).getIntValue();
        const int b = token.contains ("-") ? token.fromFirstOccurrenceOf ("-", false, false).getIntValue() : a;
        for (int p = jlimit (1, 128, a); p <= jlimit (1, 128, b); ++p) out.push_back (p);
    }
    return out;
}

/**
 * Mensajes MIDI escritos por el usuario, separados por comas, punto y coma o saltos de línea:
 *   CC91=0 · PC 5 · F0 43 10 7F 03 40 01 23 00 F7 (sistema exclusivo en hexadecimal).
 * Devuelve false y `error` con el primero que no entiende.
 */
bool parseMidiList (const String& text, int channel, std::vector<MidiMessage>& out, String& error)
{
    out.clear();
    for (auto token : StringArray::fromTokens (text, ",;\n", ""))
    {
        token = token.trim().toUpperCase();
        if (token.isEmpty()) continue;
        if (token.startsWith ("CC"))
        {
            const auto body = token.substring (2).replaceCharacter ('=', ' ').trim();
            const auto parts = StringArray::fromTokens (body, " ", "");
            const int cc = parts.size() > 0 ? parts[0].getIntValue() : -1, v = parts.size() > 1 ? parts[1].getIntValue() : -1;
            if (parts.size() != 2 || cc < 0 || cc > 127 || v < 0 || v > 127) { error = token; return false; }
            out.push_back (MidiMessage::controllerEvent (channel, cc, v));
        }
        else if (token.startsWith ("PC"))
        {
            const int pc = token.substring (2).trim().getIntValue();
            if (pc < 1 || pc > 128) { error = token; return false; }
            out.push_back (MidiMessage::programChange (channel, pc - 1));
        }
        else if (token.startsWith ("F0"))
        {
            MemoryBlock bytes;
            for (const auto& h : StringArray::fromTokens (token, " ", ""))
            {
                if (h.length() != 2 || ! h.containsOnly ("0123456789ABCDEF")) { error = token; return false; }
                const auto b = uint8 (h.getHexValue32());
                bytes.append (&b, 1);
            }
            const auto* d = static_cast<const uint8*> (bytes.getData());
            if (bytes.getSize() < 3 || d[bytes.getSize() - 1] != 0xF7) { error = token; return false; }
            out.push_back (MidiMessage::createSysExMessage (d + 1, int (bytes.getSize()) - 2));
        }
        else { error = token; return false; }
    }
    return true;
}

File defaultFolder()
{
    return File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Betracsec Keys").getChildFile ("Muestras");
}

std::unique_ptr<PropertiesFile> openSettings()
{
    PropertiesFile::Options o;
    o.applicationName = "Betracsec Keys";
    o.filenameSuffix = "muestreador";
    o.folderName = "Betracsec Keys"; // carpeta de datos: conserva el nombre de antes (BeTracks Keys se llamó así) para no perder lo guardado
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<PropertiesFile> (o);
}

// ── Grabación: copia las dos entradas elegidas a un búfer mientras está armada ─────────────────
class Recorder : public AudioIODeviceCallback
{
public:
    /** Reserva el búfer (sin grabar todavía: el audio no lo toca). */
    void prepare (int channels, int capacity)
    {
        recording_ = false;
        Thread::sleep (30);   // que termine un posible bloque en curso
        buffer_.setSize (channels, capacity, false, true, false);
    }
    void arm()
    {
        writePos_ = 0;
        recording_ = true;
    }
    void disarm() { recording_ = false; }
    int position() const { return writePos_.load(); }
    const AudioBuffer<float>& buffer() const { return buffer_; }
    float takeMeter() { return meter_.exchange (0.0f); }
    double sampleRate() const { return sampleRate_.load(); }

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) FloatVectorOperations::clear (out[c], n);
        float peak = 0;
        for (int c = 0; c < numIn; ++c) if (in[c] != nullptr) peak = std::max (peak, FloatVectorOperations::findMaximum (in[c], n) - 0.0f);
        for (int c = 0; c < numIn; ++c) if (in[c] != nullptr) peak = std::max (peak, -FloatVectorOperations::findMinimum (in[c], n));
        if (peak > meter_.load()) meter_ = peak;
        if (! recording_) return;
        const int pos = writePos_.load();
        const int count = std::min (n, buffer_.getNumSamples() - pos);
        if (count <= 0) return;
        for (int c = 0; c < buffer_.getNumChannels(); ++c)
        {
            const int src = std::min (c, numIn - 1);
            if (src >= 0 && in[src] != nullptr) buffer_.copyFrom (c, pos, in[src], count);
        }
        writePos_ = pos + count;
    }
    void audioDeviceAboutToStart (AudioIODevice* d) override { sampleRate_ = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override {}

private:
    AudioBuffer<float> buffer_;
    std::atomic<int> writePos_ { 0 };
    std::atomic<bool> recording_ { false };
    std::atomic<float> meter_ { 0 };
    std::atomic<double> sampleRate_ { 48000 };
};

struct Settings
{
    String midiOut, audioDevice, name = "Mi instrumento", programs = "1";
    int channel = 1, inputPair = 0, lowNote = 36, highNote = 96, step = 3, layers = 3, msb = 63, lsb = 0;
    bool mono = false, changeProgram = false, loops = false;
    String preMidi = "CC91=0, CC93=0";   ///< mensajes que se envían antes de grabar cada voz
    double hold = 3.0, tail = 2.0;
    File folder = defaultFolder();
};

// ── El trabajo: toca, graba y guarda ─────────────────────────────────────────────────────────────
class Job : public Thread
{
public:
    Job (Recorder& r, std::unique_ptr<MidiOutput> midi, Settings s, bool testOnly)
        : Thread ("Muestreador"), rec_ (r), midi_ (std::move (midi)), s_ (std::move (s)), test_ (testOnly) {}
    ~Job() override { stopThread (4000); }

    std::atomic<double> progress { 0 };
    String status() const { const ScopedLock l (lock_); return status_; }
    std::vector<File> saved() const { const ScopedLock l (lock_); return saved_; }
    bool failed() const { return failed_.load(); }

    void run() override
    {
        {
            const double sr = rec_.sampleRate();
            rec_.prepare (s_.mono ? 1 : 2, int ((std::max (s_.hold, 1.5) + s_.tail + 1.0) * sr));
        }
        if (test_)
        {
            // Probar con la voz que se va a grabar: la primera de la lista si Keys cambia de voz.
            if (s_.changeProgram)
                if (const auto list = parsePrograms (s_.programs); ! list.empty())
                {
                    selectProgram (list.front());
                    setStatus ("Voz " + String (list.front()) + " (MSB " + String (s_.msb) + ", LSB " + String (s_.lsb) + ")…");
                }
            sendDry();
            testNote();
            allOff();
            return;
        }
        const auto notes = noteList();
        const auto vels = velocityLayers (s_.layers);
        const auto programs = s_.changeProgram ? parsePrograms (s_.programs) : std::vector<int> { -1 };
        if (programs.empty()) { fail ("No hay programas que muestrear."); return; }
        const int total = int (programs.size() * notes.size() * vels.size());
        int done = 0;
        for (int program : programs)
        {
            if (threadShouldExit()) break;
            if (program > 0) selectProgram (program);
            sendDry();
            // Antes de grabar: el caso más fuerte no puede recortar (si no, media hora de tomas saturadas).
            {
                const auto c = loudestCheck();
                if (threadShouldExit()) break;
                if (! c.silent && (c.clipped || c.peak > 0.95f))
                {
                    fail (levelAdvice (c) + String::fromUTF8 ("\nNo se ha grabado nada: baja la ganancia y vuelve a pulsar «Muestrear»."));
                    allOff();
                    return;
                }
            }
            std::vector<bk::bkm::Stroke> strokes;
            bool warnedClip = false;
            for (size_t ni = 0; ni < notes.size() && ! threadShouldExit(); ++ni)
                for (size_t vi = 0; vi < vels.size() && ! threadShouldExit(); ++vi)
                {
                    setStatus ("Grabando " + noteName (notes[ni]) + " · velocidad " + String (vels[vi])
                               + (program > 0 ? " · programa " + String (program) : String()));
                    auto take = record (notes[ni], vels[vi]);
                    if (threadShouldExit()) break;
                    const auto r = bk::autosample::process (take, s_.loops);
                    ++done;
                    progress = double (done) / total;
                    if (r.silent)
                    {
                        if (strokes.empty() && ni == 0 && vi == 0 && vels[vi] >= 64)
                        {
                            fail (String::fromUTF8 ("No llega sonido por la entrada elegida. Revisa que el módulo suene, el cable a la interfaz, "
                                                    "las entradas y la ganancia (prueba con «Probar»)."));
                            allOff();
                            return;
                        }
                        continue;   // una nota que no suena (fuera del rango del sonido): se salta
                    }
                    if (r.clipped)
                    {
                        fail (String::fromUTF8 ("Recorta en ") + noteName (notes[ni]) + " a velocidad " + String (vels[vi])
                              + String::fromUTF8 (". Detenido para no guardar tomas saturadas: baja la ganancia y vuelve a empezar."));
                        allOff();
                        return;
                    }
                    juce::ignoreUnused (warnedClip);
                    bk::bkm::Stroke st;
                    st.audio = r.audio;
                    st.loops = r.loops;
                    st.loopStart = r.loopStart;
                    st.loopEnd = r.loopEnd;
                    st.root = notes[ni];
                    st.low = ni == 0 ? 0 : (notes[ni - 1] + notes[ni]) / 2 + 1;
                    st.high = ni + 1 == notes.size() ? 127 : (notes[ni] + notes[ni + 1]) / 2;
                    st.velLow = vi == 0 ? 1 : (vels[vi - 1] + vels[vi]) / 2 + 1;
                    st.velHigh = vi + 1 == vels.size() ? 127 : (vels[vi] + vels[vi + 1]) / 2;
                    st.velocity = vels[vi];
                    strokes.push_back (std::move (st));
                }
            allOff();
            if (threadShouldExit()) { setStatus ("Detenido. No se ha guardado nada."); return; }
            // Notas saltadas: la zona de la vecina se amplía para cubrirlas.
            fillGaps (strokes);
            const auto name = program > 0 && programs.size() > 1 ? s_.name + " " + String (program).paddedLeft ('0', 3) : s_.name;
            const auto file = s_.folder.getChildFile (File::createLegalFileName (name) + ".bkm").getNonexistentSibling();
            s_.folder.createDirectory();
            String err;
            if (! bk::bkm::write (file, name, String::fromUTF8 ("Muestreado por el usuario con BeTracks Keys"),
                                  rec_.sampleRate(), strokes, err))
            {
                fail (err);
                return;
            }
            {
                const ScopedLock l (lock_);
                saved_.push_back (file);
            }
            setStatus ("Guardado: " + file.getFileName() + " (" + String (file.getSize() / 1.0e6, 1) + " MB, "
                       + String (int (strokes.size())) + " muestras)");
        }
        progress = 1.0;
    }

private:
    std::vector<int> noteList() const
    {
        std::vector<int> n;
        for (int k = s_.lowNote; k <= s_.highNote; k += std::max (1, s_.step)) n.push_back (k);
        if (n.empty() || n.back() != s_.highNote) n.push_back (s_.highNote);
        return n;
    }

    static void fillGaps (std::vector<bk::bkm::Stroke>& strokes)
    {
        // Para cada capa de velocidad, los rangos de tecla se extienden hasta tocarse.
        std::map<int, std::vector<bk::bkm::Stroke*>> byVel;
        for (auto& s : strokes) byVel[s.velLow].push_back (&s);
        for (auto& [v, list] : byVel)
        {
            std::sort (list.begin(), list.end(), [] (auto* a, auto* b) { return a->root < b->root; });
            for (size_t i = 0; i < list.size(); ++i)
            {
                list[i]->low = i == 0 ? 0 : (list[i - 1]->root + list[i]->root) / 2 + 1;
                list[i]->high = i + 1 == list.size() ? 127 : (list[i]->root + list[i + 1]->root) / 2;
            }
        }
    }

    bk::autosample::Take record (int note, int velocity)
    {
        const double sr = rec_.sampleRate();
        const int preRoll = int (0.08 * sr), hold = int (s_.hold * sr), tail = int (s_.tail * sr);
        juce::ignoreUnused (hold);
        rec_.arm();
        // Preámbulo: ruido de fondo.
        const auto t0 = Time::getMillisecondCounterHiRes();
        while (rec_.position() < preRoll && ! threadShouldExit() && Time::getMillisecondCounterHiRes() - t0 < 2000) sleep (2);
        send (MidiMessage::noteOn (s_.channel, note, uint8 (velocity)));
        wait (int (s_.hold * 1000));
        const int noteOffAt = rec_.position();
        send (MidiMessage::noteOff (s_.channel, note));
        // Cola: hasta que cae al ruido (dos lecturas seguidas) o al máximo.
        const auto& buf = rec_.buffer();
        const float noise = bk::autosample::rmsOf (buf, 0, std::max (1, preRoll - int (0.01 * sr)));
        float peak = 0;
        for (int c = 0; c < buf.getNumChannels(); ++c) peak = std::max (peak, buf.getMagnitude (c, 0, std::max (1, noteOffAt)));
        int quiet = 0;
        while (! threadShouldExit())
        {
            sleep (50);
            const int pos = rec_.position();
            if (pos - noteOffAt >= tail || pos >= buf.getNumSamples() - 1) break;
            const float level = bk::autosample::rmsOf (buf, pos - int (0.05 * sr), int (0.05 * sr));
            quiet = level < std::max (noise * 2.0f, peak * 0.001f) && pos - noteOffAt > int (0.15 * sr) ? quiet + 1 : 0;
            if (quiet >= 2) break;
        }
        rec_.disarm();
        bk::autosample::Take t;
        const int n = rec_.position();
        t.audio.setSize (buf.getNumChannels(), std::max (1, n));
        for (int c = 0; c < buf.getNumChannels(); ++c) t.audio.copyFrom (c, 0, buf, c, 0, std::max (1, n));
        t.noteOffAt = noteOffAt;
        t.preRoll = preRoll;
        t.sampleRate = sr;
        wait (120);
        return t;
    }

    struct LevelCheck { float peak = 0, noise = 0; int note = 60; bool clipped = false, silent = true; bk::autosample::Take loudest; };

    /** El caso más fuerte que se va a grabar: la velocidad máxima en la nota más grave, Do4 y la más aguda. */
    LevelCheck loudestCheck()
    {
        LevelCheck c;
        const int vel = velocityLayers (s_.layers).back();
        const double hold = s_.hold;
        s_.hold = 1.2;
        for (int note : { s_.lowNote, 60, s_.highNote })
        {
            if (threadShouldExit()) break;
            if (note < s_.lowNote || note > s_.highNote) continue;
            setStatus ("Comprobando el nivel: " + noteName (note) + " a velocidad " + String (vel) + String::fromUTF8 ("…"));
            auto t = record (note, vel);
            const auto r = bk::autosample::process (t, false);
            if (r.silent) continue;
            if (c.silent || r.peak > c.peak) { c.peak = r.peak; c.note = note; c.loudest = std::move (t); }
            c.noise = std::max (c.noise, r.noise);
            c.clipped = c.clipped || r.clipped;
            c.silent = false;
        }
        s_.hold = hold;
        return c;
    }

    String levelAdvice (const LevelCheck& c) const
    {
        const auto db = [] (float x) { return String (Decibels::gainToDecibels (x, -120.0f), 1) + " dB"; };
        String msg = (s_.changeProgram && ! parsePrograms (s_.programs).empty() ? "Voz " + String (parsePrograms (s_.programs).front()) + ". " : String())
                   + "Pico " + db (c.peak) + " (" + noteName (c.note) + " a velocidad " + String (velocityLayers (s_.layers).back())
                   + String::fromUTF8 (", lo más fuerte que se grabará) · ruido de fondo ") + db (c.noise) + ". ";
        if (c.clipped || c.peak > 0.95f) msg << String::fromUTF8 ("Recorta: baja la ganancia de la entrada.");
        else if (c.peak < 0.25f) msg << String::fromUTF8 ("Nivel bajo: sube la ganancia (ideal: pico entre −6 y −1 dB).");
        else msg << String::fromUTF8 ("Nivel correcto.");
        return msg;
    }

    void testNote()
    {
        const auto c = loudestCheck();
        progress = 1.0;
        if (c.silent) { fail (String::fromUTF8 ("No llega sonido. Revisa la salida MIDI, el canal, el cable a la interfaz y las entradas elegidas.")); return; }
        setStatus (levelAdvice (c) + "\n" + stereoReport (c.loudest));
    }

    /** ¿Llega estéreo? Nivel de cada entrada y parecido entre ellas durante la nota. */
    static String stereoReport (const bk::autosample::Take& t)
    {
        const auto& a = t.audio;
        if (a.getNumChannels() < 2) return String::fromUTF8 ("Grabando en mono (una sola entrada elegida).");
        const int from = t.preRoll, to = std::max (from + 1, t.noteOffAt);
        double ll = 0, rr = 0, lr = 0, diff = 0;
        for (int i = from; i < to; ++i)
        {
            const double l = a.getSample (0, i), r = a.getSample (1, i);
            ll += l * l; rr += r * r; lr += l * r; diff += (l - r) * (l - r);
        }
        const double rmsL = std::sqrt (ll / (to - from)), rmsR = std::sqrt (rr / (to - from));
        const double corr = ll > 0 && rr > 0 ? lr / std::sqrt (ll * rr) : 0.0;
        const auto dB = [] (double x) { return String (Decibels::gainToDecibels (float (x), -120.0f), 1) + " dB"; };
        String s = "Entrada izquierda " + dB (rmsL) + " · derecha " + dB (rmsR) + String::fromUTF8 (" · parecido ") + String (corr, 3) + ". ";
        if (rmsR < rmsL * 0.05) return s + String::fromUTF8 ("Solo llega sonido a la entrada izquierda: revisa el cable de la salida R del módulo y la entrada derecha.");
        if (rmsL < rmsR * 0.05) return s + String::fromUTF8 ("Solo llega sonido a la entrada derecha: revisa el cable de la salida L del módulo y la entrada izquierda.");
        if (corr > 0.999 && diff < 1.0e-6 * ll + 1.0e-12)
            return s + String::fromUTF8 ("Las dos entradas reciben la misma señal: llega en mono. Si la voz es estéreo, revisa que el módulo tenga conectadas "
                                         "las dos salidas (L/MONO y R) o que la interfaz no esté sumando las entradas.");
        if (corr > 0.995) return s + String::fromUTF8 ("Casi mono: la voz es muy centrada (prueba con una voz con chorus o un piano estéreo).");
        return s + String::fromUTF8 ("Llega en estéreo.");
    }

    void send (const MidiMessage& m) { if (midi_ != nullptr) midi_->sendMessageNow (m); }
    /** Banco (MSB/LSB) y programa 1..128; espera a que el módulo cargue la voz. */
    void selectProgram (int program)
    {
        send (MidiMessage::controllerEvent (s_.channel, 0, s_.msb));
        send (MidiMessage::controllerEvent (s_.channel, 32, s_.lsb));
        send (MidiMessage::programChange (s_.channel, program - 1));
        wait (700);
    }
    /** Los mensajes del usuario antes de grabar (p. ej. apagar la reverb y el chorus del módulo). */
    void sendDry()
    {
        std::vector<MidiMessage> list;
        String bad;
        if (! parseMidiList (s_.preMidi, s_.channel, list, bad) || list.empty()) return;
        for (const auto& m : list) { send (m); wait (20); }
        wait (150);
    }
    void allOff()
    {
        send (MidiMessage::allNotesOff (s_.channel));
        send (MidiMessage::allSoundOff (s_.channel));
    }
    void wait (int ms)
    {
        const auto end = Time::getMillisecondCounterHiRes() + ms;
        while (! threadShouldExit() && Time::getMillisecondCounterHiRes() < end) sleep (5);
    }
    void setStatus (const String& s) { const ScopedLock l (lock_); status_ = warning_.isEmpty() ? s : s + "  ·  " + warning_; }
    void setWarning (const String& s) { const ScopedLock l (lock_); warning_ = s; }
    void fail (const String& s) { failed_ = true; const ScopedLock l (lock_); status_ = s; }

    Recorder& rec_;
    std::unique_ptr<MidiOutput> midi_;
    Settings s_;
    bool test_;
    CriticalSection lock_;
    String status_, warning_;
    std::vector<File> saved_;
    std::atomic<bool> failed_ { false };
};

// ── Interfaz ─────────────────────────────────────────────────────────────────────────────────────
class Content : public Component, private Timer
{
public:
    explicit Content (std::function<void (const File&)> onSaved) : onSaved_ (std::move (onSaved)), bar_ (progress_)
    {
        settings_ = openSettings();
        load();
        auto label = [this] (Label& l, const char* text)
        {
            l.setText (String::fromUTF8 (text), dontSendNotification);
            l.setJustificationType (Justification::centredRight);
            l.setColour (Label::textColourId, KeysLookAndFeel::dim);
            addAndMakeVisible (l);
        };
        label (midiLabel_, "Salida MIDI");
        label (channelLabel_, "Canal");
        label (audioLabel_, "Entrada de audio");
        label (inputsLabel_, "Entradas");
        label (voiceLabel_, "Voz");
        label (msbLabel_, "MSB");
        label (lsbLabel_, "LSB");
        label (programsLabel_, "Programas");
        label (nameLabel_, "Nombre");
        label (notesLabel_, "Notas");
        label (toLabel_, "a");
        label (stepLabel_, "cada");
        label (layersLabel_, "Capas de velocidad");
        label (holdLabel_, "Nota (s)");
        label (tailLabel_, "Cola máx. (s)");
        label (folderLabel_, "Guardar en");

        for (auto* c : { &midiOut_, &channel_, &audioDevice_, &inputs_, &low_, &high_, &step_, &layers_ }) addAndMakeVisible (c);
        for (int ch = 1; ch <= 16; ++ch) channel_.addItem (String (ch), ch);
        for (int n = 12; n <= 108; ++n) { low_.addItem (noteName (n), n + 1); high_.addItem (noteName (n), n + 1); }
        for (int st : { 1, 2, 3, 4, 6, 12 }) step_.addItem (String (st) + (st == 1 ? " semitono" : " semitonos"), st);
        for (int l = 1; l <= 8; ++l) layers_.addItem (String (l), l);
        layers_.setTooltip (String::fromUTF8 ("Pianos: 6 a 8 capas para que la dinámica cambie de forma natural; órganos y pads: 1 o 2."));
        channel_.setSelectedId (s_.channel, dontSendNotification);
        low_.setSelectedId (s_.lowNote + 1, dontSendNotification);
        high_.setSelectedId (s_.highNote + 1, dontSendNotification);
        step_.setSelectedId (s_.step, dontSendNotification);
        layers_.setSelectedId (s_.layers, dontSendNotification);
        for (auto* c : { &low_, &high_, &step_, &layers_ }) c->onChange = [this] { updateEstimate(); };

        changeProgram_.setButtonText (String::fromUTF8 ("Cambiar de voz desde Keys (si no, graba la voz que tenga el módulo)"));
        changeProgram_.setToggleState (s_.changeProgram, dontSendNotification);
        changeProgram_.onClick = [this] { updateEnabled(); updateEstimate(); };
        loops_.setButtonText (String::fromUTF8 ("Bucle para sonidos sostenidos (órganos, pads, cuerdas): librerías mucho más pequeñas"));
        loops_.setToggleState (s_.loops, dontSendNotification);
        for (auto* b : { &changeProgram_, &loops_ }) addAndMakeVisible (b);
        label (preLabel_, "Antes de grabar");
        preMidi_.setTooltip (String::fromUTF8 ("Mensajes MIDI que se envían tras elegir cada voz: CC91=0, PC 5 o sistema exclusivo en hexadecimal "
                                               "(F0 … F7), separados por comas."));
        templates_.setButtonText (String::fromUTF8 ("Plantillas ▾"));
        templates_.onClick = [this] { showTemplates(); };
        addAndMakeVisible (templates_);

        auto editor = [this] (TextEditor& e, const String& text)
        {
            e.setText (text, false);
            e.setColour (TextEditor::backgroundColourId, KeysLookAndFeel::panel);
            e.setColour (TextEditor::outlineColourId, KeysLookAndFeel::stroke);
            e.onTextChange = [this] { updateEstimate(); };
            addAndMakeVisible (e);
        };
        editor (msb_, String (s_.msb));
        editor (lsb_, String (s_.lsb));
        editor (programs_, s_.programs);
        editor (name_, s_.name);
        editor (preMidi_, s_.preMidi);
        programs_.setTooltip (String::fromUTF8 ("Números de programa 1..128, p. ej. «1-8, 12». Cada uno se guarda como una librería."));
        msb_.setTooltip (String::fromUTF8 ("Selección de banco (MSB/LSB). Cada fabricante numera sus bancos a su manera: "
                                           "búscalos en la lista de datos (Data List) del manual de tu instrumento."));

        auto slider = [this] (Slider& s, double lo, double hi, double v)
        {
            s.setSliderStyle (Slider::LinearHorizontal);
            s.setTextBoxStyle (Slider::TextBoxRight, false, 52, 22);
            s.setRange (lo, hi, 0.5);
            s.setValue (v, dontSendNotification);
            s.onValueChange = [this] { updateEstimate(); };
            addAndMakeVisible (s);
        };
        slider (hold_, 0.5, 12.0, s_.hold);
        slider (tail_, 0.5, 10.0, s_.tail);

        folder_.setText (s_.folder.getFullPathName(), dontSendNotification);
        folder_.setColour (Label::textColourId, KeysLookAndFeel::text);
        addAndMakeVisible (folder_);
        chooseFolder_.setButtonText (String::fromUTF8 ("Cambiar…"));
        chooseFolder_.onClick = [this] { pickFolder(); };
        addAndMakeVisible (chooseFolder_);

        test_.setButtonText ("Probar");
        test_.setTooltip (String::fromUTF8 ("Toca un Do4 y mide el nivel y el ruido de la entrada."));
        start_.setButtonText ("Muestrear");
        stop_.setButtonText ("Detener");
        test_.onClick = [this] { begin (true); };
        start_.onClick = [this] { begin (false); };
        stop_.onClick = [this] { job_.reset(); status_.setText ("Detenido.", dontSendNotification); updateEnabled(); };
        for (auto* b : { &test_, &start_, &stop_ }) addAndMakeVisible (b);
        addAndMakeVisible (bar_);
        status_.setColour (Label::textColourId, KeysLookAndFeel::text);
        status_.setJustificationType (Justification::topLeft);
        estimate_.setColour (Label::textColourId, KeysLookAndFeel::dim);
        estimate_.setJustificationType (Justification::topLeft);
        note_.setColour (Label::textColourId, KeysLookAndFeel::dim);
        note_.setFont (FontOptions (12.0f));
        note_.setJustificationType (Justification::topLeft);
        note_.setText (String::fromUTF8 ("Lo que grabes es el sonido de tu instrumento: úsalo tú, no lo compartas ni lo vendas."), dontSendNotification);
        for (auto* l : { &status_, &estimate_, &note_ }) addAndMakeVisible (l);

        // MIDI y audio.
        midiDevices_ = MidiOutput::getAvailableDevices();
        int pick = 0;
        for (int i = 0; i < midiDevices_.size(); ++i)
        {
            midiOut_.addItem (midiDevices_[i].name, i + 1);
            const auto n = midiDevices_[i].name.toLowerCase();
            // Un Motif por USB da varios puertos: el 1 es el del generador de sonido.
            if (midiDevices_[i].name == s_.midiOut) pick = i + 1;
            else if (s_.midiOut.isEmpty() && pick == 0 && n.contains ("motif") && ! n.contains ("port2") && ! n.contains ("port3") && ! n.contains ("port4")) pick = i + 1;
        }
        if (pick == 0 && midiDevices_.size() > 0) pick = 1;
        midiOut_.setSelectedId (pick, dontSendNotification);

        deviceManager_.initialise (2, 0, nullptr, true);
        if (auto* type = deviceManager_.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            const auto names = type->getDeviceNames (true);
            int dev = 0, guess = 0;
            for (int i = 0; i < names.size(); ++i)
            {
                audioDevice_.addItem (names[i], i + 1);
                if (names[i] == s_.audioDevice) dev = i + 1;
                // Interfaces habituales primero; ni el micrófono del Mac ni los dispositivos virtuales.
                if (guess == 0 && (names[i].containsIgnoreCase ("quantum") || names[i].containsIgnoreCase ("presonus")
                                   || names[i].containsIgnoreCase ("apollo") || names[i].containsIgnoreCase ("universal audio"))) guess = i + 1;
            }
            if (dev == 0) dev = guess;
            if (dev == 0 && names.size() > 0) dev = 1;
            audioDevice_.setSelectedId (dev, dontSendNotification);
        }
        audioDevice_.onChange = [this] { openDevice(); };
        inputs_.onChange = [this] { applyInputs(); };
        deviceManager_.addAudioCallback (&recorder_);
        openDevice();
        updateEnabled();
        updateEstimate();
        setSize (760, 680);
        startTimerHz (20);
    }

    ~Content() override
    {
        job_.reset();
        deviceManager_.removeAudioCallback (&recorder_);
        save();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (KeysLookAndFeel::bg);
        // Medidor de entrada.
        auto m = meterArea_.toFloat();
        g.setColour (KeysLookAndFeel::panel);
        g.fillRoundedRectangle (m, 3);
        const float db = Decibels::gainToDecibels (meter_, -60.0f);
        const float w = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * m.getWidth();
        g.setColour (meter_ > 0.99f ? Colours::red : db > -6 ? Colours::orange : KeysLookAndFeel::accent);
        g.fillRoundedRectangle (m.withWidth (w), 3);
        g.setColour (KeysLookAndFeel::dim);
        g.setFont (FontOptions (11.0f));
        g.drawText ("Entrada", meterArea_.translated (0, -16).withHeight (14), Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        const int lw = 130, rowH = 28, gap = 8;
        auto row = [&] { auto x = r.removeFromTop (rowH); r.removeFromTop (gap); return x; };
        // Sin valor por defecto que use `lw`: MSVC no admite una variable local como argumento por defecto (C2587).
        auto lab = [&] (Rectangle<int>& x, Label& l, int w = -1) { l.setBounds (x.removeFromLeft (w < 0 ? lw : w).withTrimmedRight (8)); };

        auto x = row(); lab (x, midiLabel_); midiOut_.setBounds (x.removeFromLeft (300)); lab (x, channelLabel_, 70); channel_.setBounds (x.removeFromLeft (70));
        x = row(); lab (x, audioLabel_); audioDevice_.setBounds (x.removeFromLeft (300)); lab (x, inputsLabel_, 70); inputs_.setBounds (x.removeFromLeft (150));
        r.removeFromTop (4);
        x = row(); lab (x, voiceLabel_); changeProgram_.setBounds (x);
        x = row(); x.removeFromLeft (lw); lab (x, msbLabel_, 40); msb_.setBounds (x.removeFromLeft (50)); lab (x, lsbLabel_, 50); lsb_.setBounds (x.removeFromLeft (50));
        lab (x, programsLabel_, 90); programs_.setBounds (x.removeFromLeft (180));
        x = row(); lab (x, nameLabel_); name_.setBounds (x.removeFromLeft (300));
        r.removeFromTop (4);
        x = row(); lab (x, notesLabel_); low_.setBounds (x.removeFromLeft (80)); lab (x, toLabel_, 28); high_.setBounds (x.removeFromLeft (80));
        lab (x, stepLabel_, 50); step_.setBounds (x.removeFromLeft (130));
        x = row(); lab (x, layersLabel_); layers_.setBounds (x.removeFromLeft (80));
        x = row(); lab (x, holdLabel_); hold_.setBounds (x.removeFromLeft (220)); lab (x, tailLabel_, 110); tail_.setBounds (x.removeFromLeft (200));
        x = row(); x.removeFromLeft (lw); loops_.setBounds (x);
        x = row(); lab (x, preLabel_); templates_.setBounds (x.removeFromRight (130)); preMidi_.setBounds (x.withTrimmedRight (8));
        x = row(); lab (x, folderLabel_); chooseFolder_.setBounds (x.removeFromRight (100)); folder_.setBounds (x.withTrimmedRight (8));
        x = r.removeFromTop (34); r.removeFromTop (gap);
        x.removeFromLeft (lw);
        test_.setBounds (x.removeFromLeft (100)); x.removeFromLeft (8);
        start_.setBounds (x.removeFromLeft (130)); x.removeFromLeft (8);
        stop_.setBounds (x.removeFromLeft (100));
        estimate_.setBounds (r.removeFromTop (20));
        r.removeFromTop (18);
        meterArea_ = r.removeFromTop (10);
        r.removeFromTop (gap);
        bar_.setBounds (r.removeFromTop (18));
        r.removeFromTop (gap);
        note_.setBounds (r.removeFromBottom (18));
        status_.setBounds (r);
    }

private:
    void timerCallback() override
    {
        const float m = recorder_.takeMeter();
        meter_ = std::max (m, meter_ * 0.85f);
        repaint (meterArea_.expanded (2, 18));
        if (job_ != nullptr)
        {
            progress_ = job_->progress.load();
            status_.setText (job_->status(), dontSendNotification);
            if (! job_->isThreadRunning())
            {
                for (const auto& f : job_->saved()) if (onSaved_) onSaved_ (f);
                job_.reset();
                updateEnabled();
            }
        }
    }

    void openDevice()
    {
        auto setup = deviceManager_.getAudioDeviceSetup();
        setup.inputDeviceName = audioDevice_.getText();
        setup.outputDeviceName = {};
        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.clear();
        const auto err = deviceManager_.setAudioDeviceSetup (setup, true);
        inputs_.clear (dontSendNotification);
        if (auto* d = deviceManager_.getCurrentAudioDevice())
        {
            const auto names = d->getInputChannelNames();
            for (int i = 0; i + 1 < names.size(); i += 2) inputs_.addItem (String (i + 1) + "-" + String (i + 2) + String::fromUTF8 (" (estéreo)"), i + 1);
            for (int i = 0; i < names.size(); ++i) inputs_.addItem (String (i + 1) + " (mono)", 1000 + i + 1);
            const int want = s_.mono ? 1000 + s_.inputPair + 1 : s_.inputPair + 1;
            inputs_.setSelectedId (inputs_.indexOfItemId (want) >= 0 ? want : 1, dontSendNotification);
            applyInputs();
            status_.setText (d->getName() + " · " + String (d->getCurrentSampleRate() / 1000.0, 1) + " kHz", dontSendNotification);
        }
        else status_.setText (err.isNotEmpty() ? err : String::fromUTF8 ("No se pudo abrir la entrada de audio."), dontSendNotification);
    }

    void applyInputs()
    {
        const int id = inputs_.getSelectedId();
        if (id <= 0) return;
        auto setup = deviceManager_.getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        const bool mono = id > 1000;
        const int first = (mono ? id - 1000 : id) - 1;
        setup.inputChannels.setBit (first);
        if (! mono) setup.inputChannels.setBit (first + 1);
        deviceManager_.setAudioDeviceSetup (setup, true);
        s_.mono = mono;
        s_.inputPair = first;
    }

    // ── Plantillas de «Antes de grabar»: tres de serie y las del usuario (en los ajustes del muestreador) ──
    struct Template { String name, messages; };

    static std::vector<Template> builtInTemplates()
    {
        return { { "Nada", {} },
                 { String::fromUTF8 ("General MIDI: envíos de reverb y chorus a 0 (CC91, CC93)"), "CC91=0, CC93=0" },
                 { String::fromUTF8 ("Ejemplo SysEx: retorno de reverb y chorus de la voz a 0 (adáptalo al manual de tu módulo)"),
                   "F0 43 10 7F 03 40 01 23 00 F7, F0 43 10 7F 03 40 02 23 00 F7" } };
    }

    std::vector<Template> userTemplates() const
    {
        std::vector<Template> out;
        if (auto xml = settings_->getXmlValue ("templates"))
            for (auto* t : xml->getChildIterator())
                out.push_back ({ t->getStringAttribute ("name"), t->getStringAttribute ("messages") });
        return out;
    }

    void storeUserTemplates (const std::vector<Template>& list)
    {
        XmlElement xml ("TEMPLATES");
        for (const auto& t : list)
        {
            auto* e = xml.createNewChildElement ("TEMPLATE");
            e->setAttribute ("name", t.name);
            e->setAttribute ("messages", t.messages);
        }
        settings_->setValue ("templates", &xml);
        settings_->saveIfNeeded();
    }

    void showTemplates()
    {
        const auto builtIn = builtInTemplates();
        const auto mine = userTemplates();
        PopupMenu menu, remove;
        menu.addSectionHeader ("De serie");
        for (size_t i = 0; i < builtIn.size(); ++i) menu.addItem (1 + int (i), builtIn[i].name);
        if (! mine.empty())
        {
            menu.addSectionHeader ("Mis plantillas");
            for (size_t i = 0; i < mine.size(); ++i)
            {
                menu.addItem (100 + int (i), mine[i].name);
                remove.addItem (200 + int (i), mine[i].name);
            }
        }
        menu.addSeparator();
        menu.addItem (kSaveTemplate, String::fromUTF8 ("Guardar el texto actual como plantilla…"), preMidi_.getText().trim().isNotEmpty());
        if (! mine.empty()) menu.addSubMenu ("Borrar una plantilla", remove);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (templates_), [this, builtIn, mine] (int r)
        {
            if (r >= 1 && r <= int (builtIn.size())) preMidi_.setText (builtIn[size_t (r - 1)].messages, true);
            else if (r >= 100 && r < 200 && size_t (r - 100) < mine.size()) preMidi_.setText (mine[size_t (r - 100)].messages, true);
            else if (r >= 200 && size_t (r - 200) < mine.size())
            {
                auto list = mine;
                list.erase (list.begin() + (r - 200));
                storeUserTemplates (list);
            }
            else if (r == kSaveTemplate) saveTemplate();
        });
    }

    void saveTemplate()
    {
        std::vector<MidiMessage> check;
        String bad;
        if (! parseMidiList (preMidi_.getText(), 1, check, bad))
        {
            status_.setText (String::fromUTF8 ("No se puede guardar: no entiendo «") + bad + String::fromUTF8 ("»."), dontSendNotification);
            return;
        }
        auto* w = new AlertWindow (String::fromUTF8 ("Guardar plantilla"),
                                   String::fromUTF8 ("Nombre de la plantilla (p. ej. «Mi módulo sin reverb»):"), MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", {});
        w->addButton ("Guardar", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancelar", 0, KeyPress (KeyPress::escapeKey));
        const auto messages = preMidi_.getText().trim();
        w->enterModalState (true, ModalCallbackFunction::create ([this, w, messages] (int r)
        {
            const auto name = w->getTextEditorContents ("name").trim();
            if (r == 1 && name.isNotEmpty())
            {
                auto list = userTemplates();
                auto it = std::find_if (list.begin(), list.end(), [&] (const Template& t) { return t.name == name; });
                if (it != list.end()) it->messages = messages;   // mismo nombre: se actualiza
                else list.push_back ({ name, messages });
                storeUserTemplates (list);
                status_.setText (String::fromUTF8 ("Plantilla guardada: ") + name, dontSendNotification);
            }
        }), true);
    }

    static constexpr int kSaveTemplate = 99;

    void pickFolder()
    {
        chooser_ = std::make_unique<FileChooser> ("Carpeta para las librerías", s_.folder);
        chooser_->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& fc)
        {
            if (fc.getResult().isDirectory()) { s_.folder = fc.getResult(); folder_.setText (s_.folder.getFullPathName(), dontSendNotification); }
        });
    }

    void gather()
    {
        s_.midiOut = midiOut_.getText();
        s_.channel = std::max (1, channel_.getSelectedId());
        s_.audioDevice = audioDevice_.getText();
        s_.lowNote = low_.getSelectedId() - 1;
        s_.highNote = std::max (s_.lowNote, high_.getSelectedId() - 1);
        s_.step = std::max (1, step_.getSelectedId());
        s_.layers = jlimit (1, 8, layers_.getSelectedId());
        s_.hold = hold_.getValue();
        s_.tail = tail_.getValue();
        s_.loops = loops_.getToggleState();
        s_.preMidi = preMidi_.getText().trim();
        s_.changeProgram = changeProgram_.getToggleState();
        s_.msb = jlimit (0, 127, msb_.getText().getIntValue());
        s_.lsb = jlimit (0, 127, lsb_.getText().getIntValue());
        s_.programs = programs_.getText();
        s_.name = name_.getText().trim().isEmpty() ? String ("Muestras") : name_.getText().trim();
    }

    void updateEstimate()
    {
        gather();
        int notes = 0;
        for (int k = s_.lowNote; k <= s_.highNote; k += s_.step) ++notes;
        if ((s_.highNote - s_.lowNote) % s_.step != 0) ++notes;
        const int voices = s_.changeProgram ? std::max (1, int (parsePrograms (s_.programs).size())) : 1;
        const int takes = notes * s_.layers;
        const double secs = takes * (s_.hold + std::min (s_.tail, 1.5) + 0.35) * voices;
        // FLAC: el piano de prueba (344 tomas de 8,3 s) ocupó 95 MB, un 20 % del PCM.
        const double mb = takes * (s_.loops ? std::min (s_.hold, 1.6) : s_.hold + 0.3) * 48000.0 * 2 * 2 * (s_.loops ? 0.5 : 0.2) / 1.0e6;
        estimate_.setText (String (notes) + String::fromUTF8 (" notas × ") + String (s_.layers) + (s_.layers == 1 ? " capa" : " capas") + " = " + String (takes)
                           + " tomas" + (voices > 1 ? String::fromUTF8 (" × ") + String (voices) + " voces" : String()) + String::fromUTF8 (" · unos ")
                           + String (std::max (1, roundToInt (secs / 60.0))) + " min · unos " + String (roundToInt (mb)) + " MB por voz",
                           dontSendNotification);
    }

    void updateEnabled()
    {
        const bool busy = job_ != nullptr;
        for (auto* c : std::initializer_list<Component*> { &midiOut_, &channel_, &audioDevice_, &inputs_, &changeProgram_, &name_, &low_, &high_,
                                                            &step_, &layers_, &hold_, &tail_, &loops_, &preMidi_, &templates_, &chooseFolder_, &test_, &start_ })
            c->setEnabled (! busy);
        stop_.setEnabled (busy);
        for (auto* e : { &msb_, &lsb_, &programs_ }) e->setEnabled (! busy && changeProgram_.getToggleState());
    }

    void begin (bool testOnly)
    {
        gather();
        save();
        const int idx = midiOut_.getSelectedId() - 1;
        if (idx < 0 || idx >= midiDevices_.size()) { status_.setText (String::fromUTF8 ("Elige la salida MIDI del módulo."), dontSendNotification); return; }
        auto midi = MidiOutput::openDevice (midiDevices_[idx].identifier);
        if (midi == nullptr) { status_.setText ("No se pudo abrir " + midiDevices_[idx].name, dontSendNotification); return; }
        if (deviceManager_.getCurrentAudioDevice() == nullptr) { status_.setText (String::fromUTF8 ("Elige la entrada de audio."), dontSendNotification); return; }
        {
            std::vector<MidiMessage> list;
            String bad;
            if (! parseMidiList (s_.preMidi, s_.channel, list, bad))
            {
                status_.setText (String::fromUTF8 ("No entiendo este mensaje de «Antes de grabar»: ") + bad
                                 + String::fromUTF8 (". Usa CC91=0, PC 5 o F0 … F7 en hexadecimal, separados por comas."), dontSendNotification);
                return;
            }
        }
        progress_ = 0;
        job_ = std::make_unique<Job> (recorder_, std::move (midi), s_, testOnly);
        job_->startThread();
        updateEnabled();
    }

    void load()
    {
        auto& p = *settings_;
        s_.midiOut = p.getValue ("midiOut");
        s_.audioDevice = p.getValue ("audioDevice");
        s_.channel = p.getIntValue ("channel", 1);
        s_.inputPair = p.getIntValue ("inputPair", 0);
        s_.mono = p.getBoolValue ("mono", false);
        s_.lowNote = p.getIntValue ("lowNote", 36);
        s_.highNote = p.getIntValue ("highNote", 96);
        s_.step = p.getIntValue ("step", 3);
        s_.layers = p.getIntValue ("layers", 3);
        s_.hold = p.getDoubleValue ("hold", 3.0);
        s_.tail = p.getDoubleValue ("tail", 2.0);
        s_.loops = p.getBoolValue ("loops", false);
        s_.preMidi = p.getValue ("preMidi", "CC91=0, CC93=0");
        s_.changeProgram = p.getBoolValue ("changeProgram", false);
        s_.msb = p.getIntValue ("msb", 63);
        s_.lsb = p.getIntValue ("lsb", 0);
        s_.programs = p.getValue ("programs", "1");
        s_.name = p.getValue ("name", String::fromUTF8 ("Mi instrumento"));
        if (const File f (p.getValue ("folder")); f.getFullPathName().isNotEmpty()) s_.folder = f;
    }

    void save()
    {
        auto& p = *settings_;
        p.setValue ("midiOut", s_.midiOut);
        p.setValue ("audioDevice", s_.audioDevice);
        p.setValue ("channel", s_.channel);
        p.setValue ("inputPair", s_.inputPair);
        p.setValue ("mono", s_.mono);
        p.setValue ("lowNote", s_.lowNote);
        p.setValue ("highNote", s_.highNote);
        p.setValue ("step", s_.step);
        p.setValue ("layers", s_.layers);
        p.setValue ("hold", s_.hold);
        p.setValue ("tail", s_.tail);
        p.setValue ("loops", s_.loops);
        p.setValue ("preMidi", s_.preMidi);
        p.setValue ("changeProgram", s_.changeProgram);
        p.setValue ("msb", s_.msb);
        p.setValue ("lsb", s_.lsb);
        p.setValue ("programs", s_.programs);
        p.setValue ("name", s_.name);
        p.setValue ("folder", s_.folder.getFullPathName());
        p.saveIfNeeded();
    }

    std::function<void (const File&)> onSaved_;
    std::unique_ptr<PropertiesFile> settings_;
    Settings s_;
    AudioDeviceManager deviceManager_;
    Recorder recorder_;
    std::unique_ptr<Job> job_;
    Array<MidiDeviceInfo> midiDevices_;
    std::unique_ptr<FileChooser> chooser_;
    double progress_ = 0;
    float meter_ = 0;
    Rectangle<int> meterArea_;

    Label midiLabel_, channelLabel_, audioLabel_, inputsLabel_, voiceLabel_, msbLabel_, lsbLabel_, programsLabel_, nameLabel_,
          notesLabel_, toLabel_, stepLabel_, layersLabel_, holdLabel_, tailLabel_, folderLabel_, folder_, status_, estimate_, note_, preLabel_;
    ComboBox midiOut_, channel_, audioDevice_, inputs_, low_, high_, step_, layers_;
    ToggleButton changeProgram_, loops_;
    TextEditor msb_, lsb_, programs_, name_, preMidi_;
    TextButton templates_;
    Slider hold_, tail_;
    TextButton chooseFolder_, test_, start_, stop_;
    ProgressBar bar_;
};
} // namespace

AutosamplerWindow::AutosamplerWindow (std::function<void (const File&)> onSaved)
    : DocumentWindow (String::fromUTF8 ("Muestreador — BeTracks Keys"), KeysLookAndFeel::bg, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (std::move (onSaved)), true);
    setResizable (false, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

AutosamplerWindow::~AutosamplerWindow() = default;

void AutosamplerWindow::closeButtonPressed()
{
    MessageManager::callAsync ([w = Component::SafePointer<AutosamplerWindow> (this)] { delete w.getComponent(); });
}

bool AutosamplerWindow::isAvailable()
{
   #if JUCE_MAC || JUCE_WINDOWS
    return JUCEApplicationBase::isStandaloneApp();
   #else
    return false;
   #endif
}
