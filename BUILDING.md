# Building NUX

NUX targets **Windows 11 / 10 (x64)** with Visual Studio 2022, and also builds on
Linux for headless testing and CI. macOS is not supported yet.

## 1. Prerequisites

| Tool | Version | Notes |
|---|---|---|
| Visual Studio 2022 | 17.4+ | "Desktop development with C++" workload, MSVC v143 |
| CMake | 3.22+ | ships with VS 2022 |
| Git | any | for the JUCE submodule |
| Windows SDK | 10.0.20348+ | installed by the VS workload |

Optional but recommended for low-latency audio: the ASIO SDK from Steinberg, and
a driver that exposes ASIO (e.g. an audio interface's own driver).

## 2. Get the source

```bat
git clone --recurse-submodules https://github.com/NUXAudioTeam/nux-daw.git
cd nux-daw
```

JUCE is pinned to tag `9.0.5` as a submodule at `external/JUCE`. If the folder is
missing, the top-level `CMakeLists.txt` fetches that exact tag automatically with
`FetchContent`, so a plain `git clone` still builds.

## 3. Configure

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

Useful options:

| Option | Default | Meaning |
|---|---|---|
| `-DNUX_BUILD_TESTS=ON\|OFF` | `ON` | build the console test target |
| `-DCMAKE_BUILD_TYPE` | `RelWithDebInfo` | ignored for multi-config generators |

## 4. Build

```bat
cmake --build build --config Release --parallel
```

Outputs:

- `build\NUX_artefacts\Release\NUX DAW.exe`
- `build\tests\NUXTests_artefacts\Release\NUXTests.exe`

To open the solution: `cmake --open build` (or double-click `build\NUX.sln`).

## 5. Test

```bat
ctest --test-dir build -C Release --output-on-failure
```

The suite covers the project model, `.nux` round-tripping, the DSP blocks and an
end-to-end offline render (it renders to a temporary WAV and asserts the output
is audible, finite and inside range).

## 6. Run

```bat
build\NUX_artefacts\Release\"NUX DAW.exe"
```

On first launch NUX opens the default WASAPI output device. Use **AUDIO** in the
transport bar to choose another driver, device, sample rate or buffer size; the
settings are stored by the JUCE `AudioDeviceManager`.

## Troubleshooting

**"No audio output device is available"** - nothing could be opened. Open the
AUDIO dialog and pick a device explicitly; check that the output is not disabled
in Windows Sound settings.

**Crackling or dropouts** - raise the buffer size in the AUDIO dialog, close other
CPU-heavy applications, or switch to a proper ASIO driver.

**`external/JUCE` is empty after cloning** - run
`git submodule update --init --recursive`.

**CMake cannot find a C compiler** - NUX requests `C CXX` because JUCE needs C for
some platform sources. Install the VS "Desktop development with C++" workload.

**VST3 plugins do not appear** - NUX scans the per-user and system VST3 folders.
Press **SCAN VST3** in the Plugins tab and watch the progress bar; the list is
cached in `%APPDATA%\NUX DAW\plugin-cache.xml`.
