#!/bin/zsh
# Muestreador: recorte, bucle, formato .bkm y reproducción, con tomas sintéticas (no hace falta instrumento).
set -euo pipefail
cd "$(dirname "$0")/.."
J=vendor/JUCE/modules
D=(-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_USE_FLAC=1
   -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1)
mkdir -p build/tests/obj
for c in $J/juce_core/*.c $J/juce_audio_formats/*.c; do
  o=build/tests/obj/$(basename $c .c).o; [[ -f $o ]] || clang -O1 -w -I$J $D -c $c -o $o
done
clang++ -std=c++20 -O1 -w -I$J $D -o build/tests/autosample_test tests/autosample_test.cpp Source/plugin/KeysSampleFile.cpp \
  Source/core/SampleEngine.cpp Source/core/NordSample.cpp $J/juce_core/juce_core_CompilationTime.cpp build/tests/obj/*.o \
  -x objective-c++ $J/juce_core/juce_core.mm $J/juce_audio_basics/juce_audio_basics.mm $J/juce_audio_formats/juce_audio_formats.mm \
  -framework Foundation -framework CoreAudio -framework AudioToolbox -framework Accelerate -framework CoreFoundation -framework CoreMIDI \
  -framework IOKit -framework Security -framework CoreServices -framework AppKit -lz
build/tests/autosample_test
