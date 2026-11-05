#!/bin/sh
# Builds CiZ-synth (Standalone + VST3) on Linux x86_64.
# Needs a patched MAME 0.289 tree already built with build/linux_gcc (see CMakeLists.txt).
# Override with: JUCE_DIR=... MAME_ROOT=... ./build-linux.sh
set -eu

root=$(cd "$(dirname "$0")" && pwd)
build="$root/build-linux"

set --
[ -n "${JUCE_DIR:-}" ] && set -- "$@" -DJUCE_DIR="$JUCE_DIR"
[ -n "${MAME_ROOT:-}" ] && set -- "$@" -DMAME_ROOT="$MAME_ROOT"

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build" --target CiZSynth_Standalone CiZSynth_VST3

echo
echo "VST3: $build/CiZSynth_artefacts/Release/VST3/CiZ-synth.vst3"
echo "App: $build/CiZSynth_artefacts/Release/Standalone/CiZ-synth"
