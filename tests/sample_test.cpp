// Nord Samples: lector contra OpenNord (zonas, golpes, bucles, frecuencia y hash del PCM), y una pasada
// por todos los archivos de una carpeta.
//   tests/run_sample_tests.sh "<carpeta con .nsmp3/.nsmp4>"
#include "../Source/core/NordSample.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace bk::nsmp;
namespace fs = std::filesystem;

static std::vector<uint8_t> readFile (const std::string& p)
{
    std::ifstream f (p, std::ios::binary);
    return std::vector<uint8_t> ((std::istreambuf_iterator<char> (f)), {});
}

static uint32_t fnv (const Stroke& s)
{
    uint32_t h = 0x811c9dc5u;
    for (int c = 0; c < s.channels; ++c)
        for (uint32_t t = 0; t < s.frames; ++t) { h ^= uint32_t (s.pcm[size_t (t) * size_t (s.channels) + size_t (c)]); h *= 0x01000193u; }
    return h;
}

static std::string json (const Sample& s)
{
    std::string j = "{\"zones\":[";
    for (size_t i = 0; i < s.zones.size(); ++i)
    {
        const auto& z = s.zones[i];
        char buf[160];
        std::snprintf (buf, sizeof buf, "%s{\"root\":%d,\"lo\":%d,\"hi\":%d,\"vlo\":%d,\"vhi\":%d,\"id\":%u}", i ? "," : "", z.root, z.low, z.high, z.velLow, z.velHigh, z.strokeId);
        j += buf;
    }
    j += "],\"strokes\":[";
    for (size_t i = 0; i < s.strokes.size(); ++i)
    {
        const auto& st = s.strokes[i];
        char loop[160];
        const double decay = std::round (st.loopDecayDbPerSecond * 100) / 100;
        char dec[32];
        if (decay == std::floor (decay)) std::snprintf (dec, sizeof dec, "%d", int (decay)); else std::snprintf (dec, sizeof dec, "%g", decay);
        std::snprintf (loop, sizeof loop, "{\"start\":%u,\"end\":%u,\"loops\":%s,\"decay\":%s}", st.loopStart, st.frames, st.loops ? "true" : "false", dec);
        char buf[320];
        std::snprintf (buf, sizeof buf, "%s{\"id\":%u,\"ch\":%d,\"frames\":%u,\"rate\":%d,\"loop\":%s,\"hash\":%u}", i ? "," : "", st.id, st.channels, st.frames, int (st.sampleRate), loop, fnv (st));
        j += buf;
    }
    return j + "]}";
}

int main (int argc, char** argv)
{
    int bad = 0, ok = 0;
    // 1) Contra la referencia de OpenNord.
    std::ifstream golden (argv[1]);
    std::string line;
    while (std::getline (golden, line))
    {
        const auto f0 = line.find ("\"file\":\"") + 8, f1 = line.find ('"', f0);
        const std::string file = line.substr (f0, f1 - f0);
        const auto z0 = line.find ("\"zones\":");
        const std::string want = "{" + line.substr (z0);
        auto bytes = readFile (file);
        std::string err;
        auto s = decode (bytes.data(), bytes.size(), err);
        const std::string got = s ? json (*s) : "null: " + err;
        if (got == want) ++ok;
        else
        {
            ++bad;
            std::printf ("DISTINTO %s\n", fs::path (file).filename().c_str());
            size_t i = 0;
            while (i < got.size() && i < want.size() && got[i] == want[i]) ++i;
            std::printf ("  OpenNord: …%s\n  Keys:     …%s\n", want.substr (i > 60 ? i - 60 : 0, 160).c_str(), got.substr (i > 60 ? i - 60 : 0, 160).c_str());
        }
    }
    std::printf ("%d samples iguales a OpenNord (zonas, golpes, bucles, PCM), %d distintos\n", ok, bad);

    // 2) Todos los de la carpeta: que se lean enteros.
    if (argc > 2)
    {
        int files = 0, failed = 0;
        size_t strokes = 0;
        double seconds = 0;
        int64_t peak = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (const auto& e : fs::recursive_directory_iterator (argv[2]))
        {
            const auto ext = e.path().extension().string();
            if (ext != ".nsmp4" && ext != ".nsmp3") continue;
            ++files;
            auto bytes = readFile (e.path().string());
            std::string err;
            auto s = decode (bytes.data(), bytes.size(), err);
            if (! s || s->zones.empty()) { ++failed; std::printf ("  ✗ %s: %s\n", e.path().filename().c_str(), s ? "sin zonas" : err.c_str()); continue; }
            strokes += s->strokes.size();
            for (const auto& st : s->strokes)
            {
                seconds += st.frames / st.sampleRate;
                for (auto x : st.pcm) peak = std::max<int64_t> (peak, x < 0 ? -int64_t (x) : x);
            }
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("%d archivos, %d sin leer; %zu golpes, %.0f s de audio, pico %lld, en %.1f s\n", files, failed, strokes, seconds, (long long) peak, secs);
        bad += failed;
    }
    return bad ? 1 : 0;
}
