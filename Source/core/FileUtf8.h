// BeTracks Keys — abrir archivos cuya ruta viene en UTF-8 (toda ruta de Keys lo es: sale de juce::File).
//
// En Mac y Linux `std::ifstream (std::string)` y `fopen` ya entienden UTF-8. En Windows no: toman la ruta
// en la página de códigos del sistema, y una carpeta «Música» o un piano «Ñandú» no se abriría. Allí se
// pasa por std::filesystem::path construida desde UTF-8 (que usa la API ancha de Windows).
#pragma once

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace bk::io
{
inline std::filesystem::path pathFromUtf8 (const std::string& utf8)
{
    return std::filesystem::path (std::u8string (utf8.begin(), utf8.end()));
}

inline std::ifstream openRead (const std::string& utf8Path)
{
    return std::ifstream (pathFromUtf8 (utf8Path), std::ios::binary);
}

inline std::FILE* openFile (const std::string& utf8Path, const char* mode)
{
#if defined(_WIN32)
    std::wstring m (mode, mode + std::char_traits<char>::length (mode));
    return ::_wfopen (pathFromUtf8 (utf8Path).c_str(), m.c_str());
#else
    return std::fopen (utf8Path.c_str(), mode);
#endif
}

inline int removeFile (const std::string& utf8Path)
{
    std::error_code ec;
    return std::filesystem::remove (pathFromUtf8 (utf8Path), ec) ? 0 : -1;
}

inline int renameFile (const std::string& from, const std::string& to)
{
    std::error_code ec;
    std::filesystem::rename (pathFromUtf8 (from), pathFromUtf8 (to), ec);
    return ec ? -1 : 0;
}
} // namespace bk::io
