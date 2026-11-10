// Literales con acentos: "Afinación"_u8 → juce::String leído como UTF-8 (JUCE asume ASCII en const char*).
#pragma once

#include <juce_core/juce_core.h>

inline juce::String operator""_u8 (const char* s, size_t n) { return juce::String::fromUTF8 (s, int (n)); }
inline juce::String fromUtf8 (const std::string& s) { return juce::String::fromUTF8 (s.data(), int (s.size())); }

/** 2,6 MB · 148 MB · 1,2 GB */
inline juce::String formatMB (double bytes)
{
    if (bytes >= 1.0e9) return juce::String (bytes / 1.0e9, 1) + " GB";
    const double mb = bytes / 1.0e6;
    return mb < 10 ? juce::String (mb, 1) + " MB" : juce::String (juce::roundToInt (mb)) + " MB";
}
