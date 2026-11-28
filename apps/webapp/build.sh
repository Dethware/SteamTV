#!/bin/bash
# Build webapp in the SteamOS toolchain image (toolchains/steamos-3.9). Output: apps/webapp/build/
# The whole repository is mounted: the program uses ../../shared.
#
# Usage: ./build.sh [cmake build options...]
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd -- "$HERE/../.." && pwd)
IMAGE=${IMAGE:-tv/toolchains/steamos-3.9:dev}
BUILD_TYPE=${BUILD_TYPE:-RelWithDebInfo}

mkdir -p "$HERE/build"
docker run --rm --network none -u "$(id -u):$(id -g)" \
    -v "$REPO:/src:ro" -v "$HERE/build:/out" "$IMAGE" \
    sh -c "cmake -S /src/apps/webapp -B /out -G Ninja -DCMAKE_BUILD_TYPE=$BUILD_TYPE >/dev/null && cmake --build /out $*"
