// Comprueba el decodificador de BeTracks Keys contra la referencia de OpenNord (tests/golden).
//
//   tests/run_tests.sh                        → pianos sintéticos de tests/golden
//   build/tests/decoder_test <piano.npno>…     → solo lee y decodifica (pianos reales del usuario)
#include "../Source/core/Npno.h"

#include <chrono>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace bk::npno;

static std::vector<uint8_t> readFile (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    return std::vector<uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FALLO: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static void checkGolden (const std::string& dir, const std::string& id)
{
    std::printf ("%s\n", id.c_str());
    const auto bytes = readFile (dir + "/" + id + ".npno");
    std::string err;
    auto piano = parse (bytes.data(), bytes.size(), err);
    CHECK (piano.has_value(), "parse: %s", err.c_str());
    if (! piano) return;
    const auto& p = *piano;

    std::ifstream txt (dir + "/" + id + ".txt");
    std::string line;
    size_t zi = 0;
    int picks = 0;
    while (std::getline (txt, line))
    {
        std::istringstream in (line);
        std::string tag;
        in >> tag;
        std::string rest;
        std::getline (in, rest);
        if (! rest.empty() && rest[0] == ' ') rest.erase (0, 1);
        std::istringstream v (rest);
        if (tag == "name") CHECK (p.name == rest, "nombre '%s' ≠ '%s'", p.name.c_str(), rest.c_str());
        else if (tag == "variant") CHECK (p.variant == rest, "variante '%s' ≠ '%s'", p.variant.c_str(), rest.c_str());
        else if (tag == "version") CHECK (p.version == rest, "versión %s ≠ %s", p.version.c_str(), rest.c_str());
        else if (tag == "dir")
        {
            int ver, ch, bw, n; v >> ver >> ch >> bw >> n;
            CHECK (p.dirVersion == ver && p.channels == ch && p.blockWords == bw && int (p.strokes.size()) == n,
                   "directorio %d/%d/%d/%zu ≠ %d/%d/%d/%d", p.dirVersion, p.channels, p.blockWords, p.strokes.size(), ver, ch, bw, n);
        }
        else if (tag == "zone")
        {
            int root, low, high; v >> root >> low >> high;
            CHECK (zi < p.zones.size() && p.zones[zi].root == root && p.zones[zi].low == low && p.zones[zi].high == high, "zona %zu", zi);
            ++zi;
        }
        else if (tag == "stroke")
        {
            int i, zone, floor, flags; uint32_t frames, ls, ll;
            v >> i >> zone >> floor >> flags >> frames >> ls >> ll;
            const auto& s = p.strokes[size_t (i)];
            CHECK (s.zone == zone && s.floor == floor && s.flags == flags && s.frames == frames
                   && s.loopStart == (ll ? ls : s.loopStart) && s.loopLength == ll, "golpe %d", i);

            std::vector<int32_t> pcm (size_t (s.frames) * size_t (p.channels));
            std::string e;
            const bool ok = decodeStroke (bytes.data(), bytes.size(), p, s, pcm.data(), e);
            CHECK (ok, "decodificar golpe %d: %s", i, e.c_str());
            const auto ref = readFile (dir + "/" + id + ".s" + std::to_string (i) + ".pcm");
            CHECK (ref.size() == pcm.size() * 4, "golpe %d: tamaño %zu ≠ %zu", i, pcm.size() * 4, ref.size());
            if (ok && ref.size() == pcm.size() * 4)
            {
                size_t diff = 0;
                for (size_t k = 0; k < pcm.size(); ++k)
                {
                    int32_t r; std::memcpy (&r, ref.data() + k * 4, 4);
                    if (r != pcm[k]) ++diff;
                }
                CHECK (diff == 0, "golpe %d: %zu muestras distintas de %zu", i, diff, pcm.size());
            }
        }
        else if (tag == "pick")
        {
            int zone, vel, idx; v >> zone >> vel >> idx;
            CHECK (p.strokeFor (zone, vel) == idx, "capa zona %d vel %d: %d ≠ %d", zone, vel, p.strokeFor (zone, vel), idx);
            ++picks;
        }
    }
    CHECK (zi == p.zones.size(), "zonas: %zu ≠ %zu", p.zones.size(), zi);
    std::printf ("  %zu zonas, %zu golpes, %d elecciones de capa\n", p.zones.size(), p.strokes.size(), picks);
}

static void inspect (const std::string& path)
{
    const auto bytes = readFile (path);
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    auto piano = parse (bytes.data(), bytes.size(), err);
    if (! piano) { ++failures; std::printf ("%s: %s\n", path.c_str(), err.c_str()); return; }
    const auto& p = *piano;
    std::printf ("%s\n  \"%s\" (%s) v%s, tipo %d, %d canal(es), %zu zonas, %zu golpes, sin apagador desde %d\n",
                 path.c_str(), p.name.c_str(), p.variant.c_str(), p.version.c_str(), p.pianoType, p.channels,
                 p.zones.size(), p.strokes.size(), p.undampedFrom);
    size_t frames = 0;
    int bad = 0, releases = 0;
    int32_t peak = 0;
    for (const auto& s : p.strokes)
    {
        if (s.flags == 2) ++releases;
        std::vector<int32_t> pcm (size_t (s.frames) * size_t (p.channels));
        std::string e;
        if (! decodeStroke (bytes.data(), bytes.size(), p, s, pcm.data(), e)) { ++bad; std::printf ("  %s\n", e.c_str()); continue; }
        for (auto x : pcm) peak = std::max (peak, x < 0 ? -x : x);
        frames += s.frames;
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  %.1f s de audio, pico %d, %d de soltar tecla, %d dañados, decodificado en %.2f s\n",
                 frames / 44100.0, peak, releases, bad, secs);
    if (bad) ++failures;
}

int main (int argc, char** argv)
{
    if (argc > 1 && std::string (argv[1]) != "--golden")
        for (int i = 1; i < argc; ++i) inspect (argv[i]);
    else
    {
        const std::string dir = argc > 2 ? argv[2] : "tests/golden";
        for (auto id : { "T0", "T1", "T2", "T3", "M4", "MONO" }) checkGolden (dir, id);
    }
    std::printf (failures ? "\n%d fallos\n" : "\nTodo correcto\n", failures);
    return failures ? 1 : 0;
}
