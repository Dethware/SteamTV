#!/bin/bash
# Build tvshell in the SteamOS toolchain image (toolchains/steamos-3.9). Output: shell/build/
# The whole repository is mounted: the shell uses ../shared.
#
# Usage: ./build.sh [cmake build options...]
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd -- "$HERE/.." && pwd)
IMAGE=${IMAGE:-tv/toolchains/steamos-3.9:dev}
BUILD_TYPE=${BUILD_TYPE:-RelWithDebInfo}

mkdir -p "$HERE/build"
docker run --rm --network none -u "$(id -u):$(id -g)" \
    -v "$REPO:/src:ro" -v "$HERE/build:/out" "$IMAGE" \
    sh -c "cmake -S /src/shell -B /out -G Ninja -DCMAKE_BUILD_TYPE=$BUILD_TYPE >/dev/null && cmake --build /out $*"
