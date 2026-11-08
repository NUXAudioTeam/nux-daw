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

step "1/6 swap"
if [ ! -f /swapfile ]; then
    sudo -n fallocate -l 4G /swapfile && sudo -n chmod 600 /swapfile \
        && sudo -n mkswap /swapfile > /dev/null && sudo -n swapon /swapfile \
        && echo "4 GB swap added (the compiler needs more than the 2 GB of RAM)"
fi
free -m | head -3

step "2/6 toolchain"
if ! command -v cmake > /dev/null; then
    sudo -n apt-get update -qq
    sudo -n apt-get install -y cmake ninja-build pkg-config \
        libasound2-dev libfreetype-dev libfontconfig1-dev \
        libx11-dev libxext-dev libxinerama-dev libxrandr-dev \
        libxcursor-dev libxi-dev libxcomposite-dev libgtk-3-dev \
        libcurl4-openssl-dev \
        xvfb
fi
echo "cmake:  $(cmake --version | head -1)"
echo "cpus:   $(nproc)   memory: $(free -m | awk '/^Mem:/ {print $2}') MB"

step "2b/6 virtual display"
# The GUI self-tests (--undo-self-test, --device-report, --screenshot) need a display.
# Without one the app segfaults, which looks exactly like a code regression - it is not.
if ! pgrep -x Xvfb > /dev/null; then
    (Xvfb :99 -screen 0 1600x1000x24 > /dev/null 2>&1 &)
    sleep 2
    echo "started Xvfb on :99 (use DISPLAY=:99)"
else
    echo "Xvfb already running"
fi

step "3/6 git remote"
cd "$REPO" || exit 1
if ! git remote get-url origin > /dev/null 2>&1; then
    TOKEN_FILE="/tmp/tok.txt"
    if [ -f "$TOKEN_FILE" ]; then
        git remote add origin "https://$(cat "$TOKEN_FILE")@github.com/NUXAudioTeam/nux-daw.git"
        echo "remote restored from $TOKEN_FILE"
    else
        echo "WARNING: no token in $TOKEN_FILE - set the remote by hand before pushing:"
        echo "  git remote add origin https://<token>@github.com/NUXAudioTeam/nux-daw.git"
    fi
else
    echo "remote already configured"
fi

step "4/6 working tree"
git fetch origin "$BRANCH" --quiet 2>/dev/null
echo "local HEAD:  $(git log --oneline -1)"
echo "remote HEAD: $(git ls-remote origin "$BRANCH" 2>/dev/null | cut -c1-7)"
git status --short | head -20

step "5/6 configure"
# -j1 keeps peak memory under the sandbox's ~2 GB; parallel compiles get OOM-killed.
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_BUILD_PARALLEL_LEVEL=1

step "6/6 build"
if [ "${1:-}" = "--build" ]; then
    cmake --build build --target NUXTests -j1
    ./build/tests/NUXTests_artefacts/Release/NUXTests | tail -3
else
    echo "skipped (pass --build to build the tests)"
fi

echo
echo "done."

# ---------------------------------------------------------------------------
# A note on why things disappear
#
# The sandbox keeps /home/user between messages, but NOT:
#   build/            - excluded from snapshots by name
#   installed packages - cmake, ninja, the *-dev libraries, xvfb
#   /tmp              - the token file and any test media
#   .git/config       - it holds the token, so it is deliberately never saved
#
# What survives: the source, .git history (so every commit is safe) and the
# swap file only until the next reset.
#
# So the rule is: commit and push after every step. A pushed commit is the only
# thing that cannot be lost. This script then makes recovery one command.
#
# Swap: the sandbox has ~2 GB of RAM and the C++ compiler needs more, which is
# what killed the build with "Killed signal terminated program cc1plus". The
# script restores a 4 GB swap file first, which is what made the build succeed.
