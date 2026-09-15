#!/bin/sh
# Builds CZ-101 Studio (Standalone .app). Everything stays on the external disk.
set -eu

root=$(cd "$(dirname "$0")" && pwd)
build="$root/build"

export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"

if [ ! -f "$build/build.ninja" ]; then
    cmake -S "$root" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
fi

cmake --build "$build" --target CZ101Studio_Standalone

app="$build/CZ101Studio_artefacts/Release/Standalone/CZ-101 Studio.app"
echo
echo "App: $app"
