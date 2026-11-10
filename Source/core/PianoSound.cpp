#include "PianoSound.h"

#include "FileUtf8.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace bk
{
size_t PianoSound::memoryBytes() const
{
    size_t n = 0;
    for (const auto& s : strokes) n += s.pcm.size() * sizeof (int16_t);
    return n;
}

std::string PianoSound::displayName() const
{
    return meta.variant.empty() ? meta.name : meta.name + " (" + meta.variant + ")";
}

namespace
{
/** Archivo mapeado en memoria (un piano XL pasa de 500 MB: no se copia entero). */
struct MappedFile
{
    const uint8_t* data = nullptr;
    size_t size = 0;
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
    explicit MappedFile (const std::string& path)
    {
        file = ::CreateFileW (bk::io::pathFromUtf8 (path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER len {};
        if (! ::GetFileSizeEx (file, &len) || len.QuadPart <= 0) return;
        mapping = ::CreateFileMappingW (file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping == nullptr) return;
        void* p = ::MapViewOfFile (mapping, FILE_MAP_READ, 0, 0, 0);
        if (p == nullptr) return;
        data = static_cast<const uint8_t*> (p);
        size = size_t (len.QuadPart);
    }
    ~MappedFile()
    {
        if (data) ::UnmapViewOfFile (data);
        if (mapping) ::CloseHandle (mapping);
        if (file != INVALID_HANDLE_VALUE) ::CloseHandle (file);
    }
#else
    int fd = -1;
    explicit MappedFile (const std::string& path)
    {
        fd = ::open (path.c_str(), O_RDONLY);
        if (fd < 0) return;
        struct stat st {};
        if (::fstat (fd, &st) != 0 || st.st_size <= 0) return;
        void* p = ::mmap (nullptr, size_t (st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) return;
        data = static_cast<const uint8_t*> (p);
        size = size_t (st.st_size);
        ::madvise (p, size, MADV_SEQUENTIAL);
    }
    ~MappedFile()
    {
        if (data) ::munmap ((void*) data, size);
        if (fd >= 0) ::close (fd);
    }
#endif
    MappedFile (const MappedFile&) = delete;
    MappedFile& operator= (const MappedFile&) = delete;
};
} // namespace

std::unique_ptr<PianoSound> loadPiano (const std::string& path, std::string& error,
                                       std::atomic<float>* progress, const std::atomic<bool>* cancel,
                                       LoadOptions options)
{
    MappedFile file (path);
    if (! file.data) { error = "No se pudo abrir el archivo."; return nullptr; }
    auto parsed = npno::parse (file.data, file.size, error);
    if (! parsed) return nullptr;

    auto sound = std::make_unique<PianoSound>();
    sound->path = path;
    sound->meta = std::move (*parsed);
    const auto& p = sound->meta;
    sound->channels = p.channels;
    sound->strokes.resize (p.strokes.size());

    std::atomic<size_t> next { 0 }, done { 0 };
    std::atomic<bool> failed { false };
    std::string firstError;
    std::mutex errorLock;

    auto worker = [&]
    {
        std::vector<int32_t> tmp;
        for (size_t i; (i = next++) < p.strokes.size();)
        {
            if (failed || (cancel && cancel->load())) return;
            const auto& s = p.strokes[i];
            sound->strokes[i].flags = s.flags;
            sound->strokes[i].zone = s.zone;
            sound->strokes[i].floor = s.floor;
            sound->strokes[i].partner = s.partner;
            const bool wanted = s.flags == 0 || (s.flags == 1 && options.pedalDown) || (s.flags == 2 && options.releases);
            if (! wanted)
            {
                // Listas desactivadas: no se decodifican ni ocupan memoria (pcm vacío).
                if (progress) progress->store (float (++done) / float (p.strokes.size()));
                continue;
            }
            tmp.assign (size_t (s.frames) * size_t (p.channels), 0);
            std::string e;
            if (! npno::decodeStroke (file.data, file.size, p, s, tmp.data(), e))
            {
                std::lock_guard<std::mutex> g (errorLock);
                if (! failed.exchange (true)) firstError = e;
                return;
            }
            int32_t peak = 0;
            for (auto x : tmp) peak = std::max (peak, x < 0 ? -x : x);
            auto& a = sound->strokes[i];
            a.pcm.resize (tmp.size());
            const double g = peak > 0 ? 32767.0 / peak : 0.0;
            for (size_t k = 0; k < tmp.size(); ++k) a.pcm[k] = int16_t (std::lround (tmp[k] * g));
            a.frames = s.frames;
            a.zone = s.zone;
            a.flags = s.flags;
            a.loop = s.hasLoop && s.loopLength >= 4 && s.loopStart + s.loopLength <= s.frames;
            a.loopStart = s.loopStart;
            a.loopEnd = s.loopStart + s.loopLength;
            a.loopDecay = s.loopDecay > 0.0 && s.loopDecay <= 1.0 ? s.loopDecay : 1.0;
            a.preLoop = s.preLoop;
            a.damper = s.damper;
            a.damperSoft = s.damperSoft;
            if (progress) progress->store (float (++done) / float (p.strokes.size()));
        }
    };

    const unsigned n = std::max (1u, std::min (std::thread::hardware_concurrency(), 8u));
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < n; ++t) threads.emplace_back (worker);
    for (auto& t : threads) t.join();

    if (cancel && cancel->load()) { error = "Cancelado."; return nullptr; }
    if (failed) { error = "El piano está dañado: " + firstError; return nullptr; }
    sound->options = options;
    for (const auto& s : sound->strokes)
        if (! s.pcm.empty())
        {
            if (s.flags == 1) sound->hasPedalDown = true;
            if (s.flags == 2) sound->hasReleases = true;
        }
    return sound;
}
} // namespace bk
