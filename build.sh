#!/bin/sh
# Builds CZ-101 Studio (Standalone .app). Everything stays on the external disk.
set -eu

root=$(cd "$(dirname "$0")" && pwd)
build="$root/build"

export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"

# Sempre, nao so na primeira vez. O guarda `if [ ! -f build.ninja ]` deixava
# estes tres de fora quando a pasta de build ja existia -- e sem
# CMAKE_OSX_DEPLOYMENT_TARGET o CMake usa o SDK da maquina, o que produziu
# plugins exigindo macOS 26 sem ninguem pedir. Configurar de novo e barato.
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

cmake --build "$build" --target CZ101Studio_Standalone

app="$build/CZ101Studio_artefacts/Release/Standalone/CZ-101 Studio.app"
echo
echo "App: $app"
