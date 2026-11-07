#!/usr/bin/env bash
# NUX DAW - restore the workspace after the sandbox resets.
#
# The sandbox loses everything outside /home/user and drops the build directory,
# installed packages and the git remote (it holds a token, so it is never saved).
# This script rebuilds all of it with one command:
#
#     bash recover.sh            # restore + configure (fast, no full build)
#     bash recover.sh --build    # restore + configure + build the tests
#
set -u

REPO="/home/user/nux-daw"
BRANCH="${NUX_BRANCH:-feature/vst3-hosting}"

step() { printf '\n=== %s ===\n' "$1"; }

step "1/5 toolchain"
if ! command -v cmake > /dev/null; then
    sudo -n apt-get update -qq
    sudo -n apt-get install -y cmake ninja-build pkg-config \
        libasound2-dev libfreetype-dev libfontconfig1-dev \
        libx11-dev libxext-dev libxinerama-dev libxrandr-dev \
        libxcursor-dev libcurl4-openssl-dev xvfb
fi
echo "cmake:  $(cmake --version | head -1)"
echo "cpus:   $(nproc)   memory: $(free -m | awk '/^Mem:/ {print $2}') MB"

step "2/5 git remote"
cd "$REPO" || exit 1
if ! git remote get-url origin > /dev/null 2>&1; then
    TOKEN_FILE="/tmp/tok.txt"
    if [ -f "$TOKEN_FILE" ]; then
        git remote add origin "https://$(cat "$TOKEN_FILE")@github.com/hirushanethsara323-jpg/nux-daw.git"
        echo "remote restored from $TOKEN_FILE"
    else
        echo "WARNING: no token in $TOKEN_FILE - set the remote by hand before pushing:"
        echo "  git remote add origin https://<token>@github.com/hirushanethsara323-jpg/nux-daw.git"
    fi
else
    echo "remote already configured"
fi

step "3/5 working tree"
git fetch origin "$BRANCH" --quiet 2>/dev/null
echo "local HEAD:  $(git log --oneline -1)"
echo "remote HEAD: $(git ls-remote origin "$BRANCH" 2>/dev/null | cut -c1-7)"
git status --short | head -20

step "4/5 configure"
# -j1 keeps peak memory under the sandbox's ~2 GB; parallel compiles get OOM-killed.
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_BUILD_PARALLEL_LEVEL=1

step "5/5 build"
if [ "${1:-}" = "--build" ]; then
    cmake --build build --target NUXTests -j1
    ./build/tests/NUXTests_artefacts/Release/NUXTests | tail -3
else
    echo "skipped (pass --build to build the tests)"
fi

echo
echo "done."
