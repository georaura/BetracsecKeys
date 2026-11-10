#include "KeysSampleFile.h"

namespace bk::bkm
{
namespace
{
constexpr const char* kMagic = "BKMUESTR";
constexpr int kPeak = 12000;   // el reproductor espera picos de unos 8192 (Nord); algo más de nivel
}

bool write (const juce::File& file, const juce::String& name, const juce::String& source, double sampleRate,
            const std::vector<Stroke>& strokes, juce::String& error)
{
    if (strokes.empty()) { error = "No hay nada grabado."; return false; }
    float peak = 1.0e-6f;
    for (const auto& s : strokes)
        for (int c = 0; c < s.audio.getNumChannels(); ++c)
            peak = std::max (peak, s.audio.getMagnitude (c, 0, s.audio.getNumSamples()));
    const float gain = 0.89f / peak;   // −1 dBFS el golpe más fuerte

    juce::FlacAudioFormat flac;
    juce::Array<juce::var> zones, strokeInfo;
    juce::MemoryOutputStream blobs;
    for (size_t i = 0; i < strokes.size(); ++i)
    {
        const auto& s = strokes[i];
        juce::AudioBuffer<float> scaled (s.audio);
        scaled.applyGain (gain);
        juce::MemoryBlock encoded;
        {
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::MemoryOutputStream> (encoded, false);
            auto writer = flac.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                         .withSampleRate (sampleRate)
                                                         .withNumChannels (scaled.getNumChannels())
                                                         .withBitsPerSample (16)
                                                         .withQualityOptionIndex (5));
            if (writer == nullptr) { error = "No se pudo comprimir el audio (FLAC)."; return false; }
            writer->writeFromAudioSampleBuffer (scaled, 0, scaled.getNumSamples());
        }
        blobs.writeInt (int (encoded.getSize()));
        blobs.write (encoded.getData(), encoded.getSize());

        auto* z = new juce::DynamicObject();
        z->setProperty ("root", s.root);
        z->setProperty ("low", s.low);
        z->setProperty ("high", s.high);
        z->setProperty ("velLow", s.velLow);
        z->setProperty ("velHigh", s.velHigh);
        z->setProperty ("stroke", int (i));
        z->setProperty ("velocity", s.velocity);
        zones.add (juce::var (z));
        auto* st = new juce::DynamicObject();
        st->setProperty ("channels", s.audio.getNumChannels());
        st->setProperty ("frames", s.audio.getNumSamples());
        if (s.loops)
        {
            st->setProperty ("loopStart", s.loopStart);
            st->setProperty ("loopEnd", s.loopEnd);
        }
        strokeInfo.add (juce::var (st));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("name", name);
    root->setProperty ("source", source);
    root->setProperty ("created", juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("sampleRate", sampleRate);
    root->setProperty ("zones", zones);
    root->setProperty ("strokes", strokeInfo);
    const auto json = juce::JSON::toString (juce::var (root), true).toStdString();

    const auto tmp = file.getSiblingFile (file.getFileName() + ".part");
    {
        tmp.deleteFile();
        juce::FileOutputStream out (tmp);
        if (! out.openedOk()) { error = "No se puede escribir en " + file.getParentDirectory().getFullPathName(); return false; }
        out.write (kMagic, 8);
        out.writeInt (1);
        out.writeInt (int (json.size()));
        out.write (json.data(), json.size());
        out.write (blobs.getData(), blobs.getDataSize());
        out.flush();
        if (out.getStatus().failed()) { error = out.getStatus().getErrorMessage(); return false; }
    }
    if (! tmp.moveFileTo (file)) { error = "No se pudo guardar " + file.getFileName(); return false; }
    return true;
}

bool looksLikeBkm (const juce::File& file)
{
    juce::FileInputStream in (file);
    char m[8] {};
    return in.openedOk() && in.read (m, 8) == 8 && std::memcmp (m, kMagic, 8) == 0;
}

std::unique_ptr<bk::nsmp::SampleSound> read (const juce::File& file, std::string& error)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data) || data.getSize() < 16 || std::memcmp (data.getData(), kMagic, 8) != 0)
    {
        error = "No es una librería de muestras de Keys (.bkm).";
        return nullptr;
    }
    juce::MemoryInputStream in (data, false);
    in.skipNextBytes (8);
    if (in.readInt() != 1) { error = "Versión de librería no soportada."; return nullptr; }
    const int jsonLen = in.readInt();
    if (jsonLen <= 0 || jsonLen > in.getNumBytesRemaining()) { error = "Librería dañada."; return nullptr; }
    juce::MemoryBlock jsonBytes;
    in.readIntoMemoryBlock (jsonBytes, jsonLen);
    const auto root = juce::JSON::parse (jsonBytes.toString());
    const auto* strokesInfo = root["strokes"].getArray();
    const auto* zones = root["zones"].getArray();
    if (strokesInfo == nullptr || zones == nullptr) { error = "Librería dañada (índice)."; return nullptr; }

    auto s = std::make_unique<bk::nsmp::SampleSound>();
    s->path = file.getFullPathName().toStdString();
    s->name = root["name"].toString().toStdString();
    s->codec = 0;
    s->releaseUnlooped = true;
    const double rate = double (root["sampleRate"]);
    juce::FlacAudioFormat flac;
    std::vector<juce::AudioBuffer<float>> decoded;
    float peak = 1.0e-6f;
    for (const auto& info : *strokesInfo)
    {
        const int len = in.readInt();
        if (len <= 0 || len > in.getNumBytesRemaining()) { error = "Librería dañada (audio)."; return nullptr; }
        juce::MemoryBlock blob;
        in.readIntoMemoryBlock (blob, len);
        std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (blob, true), true));
        if (reader == nullptr) { error = "Librería dañada (FLAC)."; return nullptr; }
        juce::AudioBuffer<float> buf (int (reader->numChannels), int (reader->lengthInSamples));
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);
        for (int c = 0; c < buf.getNumChannels(); ++c) peak = std::max (peak, buf.getMagnitude (c, 0, buf.getNumSamples()));
        decoded.push_back (std::move (buf));
        juce::ignoreUnused (info);
    }
    const float scale = float (kPeak) / peak;
    for (size_t i = 0; i < decoded.size(); ++i)
    {
        const auto& buf = decoded[i];
        const auto& info = (*strokesInfo)[int (i)];
        bk::nsmp::SampleSound::Stroke st;
        st.channels = buf.getNumChannels();
        st.frames = uint32_t (buf.getNumSamples());
        st.sampleRate = rate > 0 ? rate : 48000.0;
        st.pcm.resize (size_t (st.frames) * size_t (st.channels));
        for (uint32_t f = 0; f < st.frames; ++f)
            for (int c = 0; c < st.channels; ++c)
                st.pcm[size_t (f) * size_t (st.channels) + size_t (c)] = int16_t (juce::jlimit (-32767.0f, 32767.0f, buf.getSample (c, int (f)) * scale));
        if (info.hasProperty ("loopStart"))
        {
            st.loops = true;
            st.loopStart = uint32_t (int (info["loopStart"]));
            st.loopEnd = uint32_t (int (info["loopEnd"]));
            if (st.loopEnd > st.frames || st.loopStart >= st.loopEnd) st.loops = false;
        }
        s->strokes.push_back (std::move (st));
    }
    for (const auto& z : *zones)
    {
        const int stroke = int (z["stroke"]);
        if (stroke < 0 || stroke >= int (s->strokes.size())) continue;
        bk::nsmp::SampleSound::Zone zone { int (z["root"]), int (z["low"]), int (z["high"]), int (z["velLow"]), int (z["velHigh"]), stroke, false };
        // Con varias capas, la velocidad de grabación (las librerías antiguas no la guardan: el centro de la capa).
        if (zone.velLow > 1 || zone.velHigh < 127)
            zone.velRoot = z.hasProperty ("velocity") ? int (z["velocity"]) : (zone.velLow + zone.velHigh) / 2;
        s->zones.push_back (zone);
    }
    if (s->zones.empty()) { error = "La librería no tiene zonas."; return nullptr; }
    return s;
}
} // namespace bk::bkm
