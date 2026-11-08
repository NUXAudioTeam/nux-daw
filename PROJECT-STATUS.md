# NUX DAW — සම්පූර්ණ තත්ත්ව වාර්තාව

> **දිනය:** 2026-11-07
> **Branch:** `feature/audio-testing` · **HEAD:** device selection + round trip (new commit)
> **Repo:** `hirushanethsara323-jpg/nux-daw` (private) · **PR:** [#4](https://github.com/hirushanethsara323-jpg/nux-daw/pull/4)
> **Build:** Windows CI — Debug ✅ / Release ✅ · Linux Release ✅

මේ ලේඛනය **එකම තැනකින් සම්පූර්ණ තත්ත්වය** දකින්න හදපු එකක්. හැම අංකයක්ම code එකේ, test output එකේ, නැත්නම් CI log එකේ බලලා තමයි ලියලා තියෙන්නේ. කිසිම දෙයක් "හොඳ වෙන්න ඇති" කියලා ලියලා නෑ.

---

## 1. එක වාක්‍යයෙන් තත්ත්වය

NUX DAW එකට **play, sequence, mix, audio clips, VST3 effects + instruments, save/load, WAV export, recording** වැඩ කරනවා — මේ සියල්ල **software level එකේ verify කරලා තියෙනවා**. ඒත් **physical speaker එකකින් හෝ microphone එකකින් කිසිම දෙයක් තාම test කරලා නෑ**, මොකද develop කරන machine එකේ audio device එකක් නෑ.

මේක ප්‍රශ්නයක් නොවෙන්න හේතුව: hardware එකට වඩා වැදගත් දේවල් (DSP නිවැරදිද, measurements ඇත්තද, signal එක round trip එකකින් නැති නොවී එනවද) hardware නැතිව verify කරන්න පුළුවන්. ඒවා **කරලා ඉවරයි**. Hardware test එක **අන්තිම පියවර** විදියට තියෙනවා — Step C.

---

## 2. NUX DAW මොකක්ද

| අයිතමය | විස්තරය |
|---|---|
| භාෂාව | C++20 |
| Framework | JUCE 9 |
| Build | CMake + Visual Studio 2022 (`windows-2022` CI) |
| Target | Windows (WASAPI, WASAPI exclusive, DirectSound, ASIO compiled in) |
| UI | Dark modern studio UI, teal accents, own `Theme.cpp` |
| Code size | `src/` — files 62, lines **15,384** |
| Tests | `tests/` — files **19**, suites 18, groups **165** (18 new: recording reliability + recovery) |
| Licence | JUCE AGPLv3-or-commercial (තීරණයක් ගන්න ඕන) |

### වැඩ කරන දේවල්

| ක්ෂේත්‍රය | තත්ත්වය |
|---|---|
| Audio device setup (WASAPI / WASAPI exclusive / DirectSound) | ✅ |
| ASIO backend compile වීම | ⚠️ compile වෙනවා, **driver එකක් test කරලා නෑ** |
| Stereo engine, track එකකට voices 16, MIDI input | ✅ |
| Built-in synth: Analog Kit, Sub Bass, Pluck, Warm Pad, Electric Keys | ✅ |
| Channel strip: EQ, compressor, delay, reverb, pan, fader, mute/solo, metering | ✅ |
| Mixer: tracks 32, buses 8 (delay/reverb sends), master limiter | ✅ |
| Step sequencer + transport (tempo, loop, play/stop/record) | ✅ |
| Piano roll note editing | ✅ |
| VST3 hosting **signal chain එකේ** (effects + instruments) | ✅ |
| Plugin parameter state project එකේ save වීම | ✅ |
| Plugin editor windows, chooser, per-slot bypass | ✅ |
| `.nux` project save/load, 24-bit WAV export | ✅ |
| Audio clip playback, import, drag-and-drop | ✅ |
| Undo / redo (`Ctrl+Z` / `Ctrl+Y`) | ✅ |
| Effect parameters editable (EQ, comp, delay, reverb) | ✅ |
| Audio input එක armed track එකකට record කිරීම | ✅ (simulated input එකෙන්; real mic ❌) |
| Track menu එකෙන් hosted VST3 instrument තෝරාගැනීම | ✅ |
| 440 Hz test tone + input/output meters | ✅ |
| Device disconnect detection + auto reopen | ✅ |

### තාම නැති දේවල්

| ක්ෂේත්‍රය | තත්ත්වය |
|---|---|
| Automation | ❌ නෑ |
| MIDI learn | ❌ නෑ |
| ASIO **verified working** | ❌ driver එකක් නෑ |
| CPU metering | ❌ නෑ |
| Latency compensation | ❌ නෑ |
| Sidechain | ❌ නෑ |
| **Virtual device තේරීම + round trip UI** | ✅ VIRTUAL AUDIO LAB block එක |
| Real microphone එකකින් recording | ❌ hardware නෑ |

---

## 3. Repository තත්ත්වය

| Branch | තත්ත්වය |
|---|---|
| `main` | `5cbc2d5` |
| `feature/v0.3-foundation` | `038e415` — PR #1 |
| `feature/audio-clips` | `527603f` — PR #2 |
| `feature/vst3-hosting` | `7ed9989` — PR #3 |
| `feature/audio-testing` | `bb4ab29` — **PR #4** (මේ වැඩේ) |

**Merge order:** `#1 → #2 → #3 → #4`

PR #4 එකේ commits 19ක් පේනවා, ඒත් ඒකෙන් **10ක්** audio-testing වැඩ සහ **9ක්** `vst3-hosting` (PR #3) එකේ. මේක branch stack එකක සාමාන්‍ය දෙයක් — ඒත් **#3 කලින් merge වෙන්න ඕන**, නැත්නම් diff එකේ අනිත් branch එකේ වැඩත් එනවා.

---

## 4. Audio testing brief එකේ §1–§7 — එකින් එක

| § | Brief එකේ ඉල්ලීම | කළ දේ | තත්ත්වය |
|---|---|---|---|
| 1 | Offline, device-independent processing tests | 440 Hz sine, frequency accuracy, peak/clipping, smooth gain ramps, stereo, buffer sizes 32–2048, sample rates, determinism | ✅ **13 groups** |
| 2 | Virtual audio device support | Windows device detect, VB-CABLE detect + `[virtual]` label, error වචන | ✅ **6 groups** (logic) · ❌ selection UI නෑ |
| 3 | Recording tests | Deterministic simulated input, ring buffer, WAV writing, start/stop/integrity, silent-input report | ✅ **7 groups** (simulated) · ❌ real mic |
| 4 | ASIO testing | Detect + compile verify; **driver එකක් ඇත්තට open කළොත් විතරයි verified** කියන්නේ | ⚠️ compile ✅ · driver ❌ |
| 5 | Audio loopback | WASAPI loopback capture හොයලා බැලුවා | ⚠️ JUCE එකේ නෑ → not performed |
| 6 | `AUDIO_TEST_REPORT.md` | passed / failed / not performed / simulated / virtual / physical / ASIO / limitations | ✅ |
| 7 | Branch, tests, Debug + Release, PR | `feature/audio-testing`, tests 114, Debug ✅ Release ✅, PR #4 | ✅ |

---

## 5. Test suite — groups 114

`ctest` entries **16**: per-suite 15ක් (`NUX_<slug>`, TIMEOUT 300) + සම්පූර්ණ suite එක (`NUXTests`, TIMEOUT 900).

| Suite | Groups | මොකද pin කරන්නේ |
|---|---|---|
| `OfflineAudioTests` | 13 | 440 Hz නිවැරදිද, level, clipping නෑ, stereo, ramps, buffer sizes, sample rates, determinism, WAV |
| `MeasurementTests` | 20 | levels, THD/THD+N, SNR, DC, correlation, crosstalk, true peak, BS.1770 loudness, spectrum, reference signals |
| `RoundTripTests` | 10 | software round trip, verification ladder, tolerance, frequency preservation, engine size guard |
| `UndoTests` | 10 | undo/redo, drag එකක් step එකක්, bounded history, plugin/clip restore |
| `PluginHostTests` | 8 | hosted effect audio path එකේ, hosted instrument MIDI, state save/restore, missing plugin, retirement queue |
| `SerializerTests` | 8 | round trip, broken JSON, newer schema, clamping, file save/load, plugin state |
| `RecordingTests` | 7 | take timeline position, not-recording = nothing, mono→stereo, silent input, abort, double start |
| `InsertParameterTests` | 7 | parameter ranges, percent round trip, audio එකට ඇත්තට බලපෑම, formatting |
| `AudioDeviceTests` | 6 | test tone 440 Hz stereo, clip නොවීම, ramps, meter, device loss report |
| `DeviceProbeTests` | 6 | real vs virtual name, VB-CABLE detect, empty inventory, ASIO honest reporting |
| `DspTests` | 5 | synth voices, IIR shelf, compressor, delay, smoothed value |
| `EngineRenderTests` | 5 | default project audible, empty pattern silent, clip render, transport loop, solo |
| `AudioClipTests` | 5 | missing files, playback, loop wrap, stereo channels |
| `ProjectModelTests` | 5 | default project, dB↔linear, colour ARGB, clip length, insert activation |
| `DeviceSelectionTests` | 13 | virtual cable pairing, cross-driver refusal, missing-device errors, skipped-never-passed |

> **සටහනක්:** `beginTest` declarations **115**ක් තියෙනවා, ඒත් full run එකක් report කරන්නේ **114** groups. එකක් report වෙන්නේ නෑ. මේක functionality එකට බලපාන්නේ නෑ, ඒත් ඇත්ත නිසා ලියනවා — TODO එකේ පොඩි item එකක් විදියට තියෙනවා.

### Test tools

| Tool | මොකටද |
|---|---|
| `NUXTests` | සම්පූර්ණ suite |
| `NUXTests --list` | suites ලැයිස්තුව |
| `NUXTests --only="round trip"` | එක group එකක් |
| `NUXTests --category=NUX` | category එකක් |
| `NUX DAW --device-report` | machine එකේ device inventory (window නොඅරිම, print කරලා exit) |

---

## 6. Measurements — වෙන කෙනෙකුගේ implementation එකක් එක්ක check කළා

තමන්ගේ code එකෙන් තමන්ගේ code එක verify කරන එකෙන් වැඩක් නෑ. ඒ නිසා loudness එක **ffmpeg ගේ `ebur128`** (libebur128 — වෙනම BS.1770 implementation එකක්) එක්ක සංසන්දනය කළා.

| Signal (1 kHz, 48 kHz) | libebur128 | NUX |
|---|---|---|
| 0 dBFS stereo | 0.0 LUFS | **+0.0067** |
| 0 dBFS mono | −3.0 LUFS | **−3.0** |
| −6 dBFS stereo | −6.0 LUFS | **−6.0** |
| −20 dBFS stereo | −20.0 LUFS | **−20.0** |
| −20 dBFS mono | −23.0 LUFS | **−23.0** |

### Implement කරලා තියෙන measurements

| Measurement | File |
|---|---|
| Peak / RMS / AES-17 | `Measurements.cpp` |
| True peak (4× oversampling) | `Measurements.cpp` |
| Loudness (ITU-R BS.1770-4) + LRA | `Measurements.cpp` |
| THD / THD+N | `Measurements.cpp` |
| SNR | `Measurements.cpp` |
| Stereo width, DC offset, correlation, crosstalk | `Measurements.cpp` |
| Hann-windowed FFT + parabolic interpolation | `Measurements.cpp` |
| Reference signals 11ක් | `TestSignals.cpp` |

Reference signals: silence, sine, square, triangle, sawtooth, white noise, pink noise, DC offset, two-tone (IMD), exponential sweep, impulse.

> **වැදගත්:** මේවා **implemented, calibrated නෙවෙයි.** කිසිම measurement එකක් certified instrumentation එකක් එක්ක calibrate කරලා නෑ. Report එකේ ඒක ලියලා තියෙනවා.

---

## 7. Round trip + "ඇත්ත කියන" ලද්ද (Verification ladder)

Signal එකක් generate කරලා, ආයේ capture කරලා, ආපු දේ මැනලා, ගිය දේ එක්ක සංසන්දනය කරන එක තමයි round trip. ඒත් මේකේ වැදගත්ම කොටස DSP එක නෙවෙයි — **reporting එක**:

```
notPerformed  <  skipped  <  simulated  <  virtualDevice  <  physicalDevice
```

| Level | අර්ථය |
|---|---|
| `notPerformed` | කිසිවක් run කළේ නෑ |
| `skipped` | run කරන්න බැරි වුණා, හේතුවක් සමඟ (device නෑ, driver නෑ) |
| `simulated` | software path එක විතරයි — engine එක ඔප්පු කරනවා, hardware ගැන **කිසිවක් නොවෙයි** |
| `virtualDevice` | virtual cable එකක් audio එක අරන් ගියා — routing ඔප්පු කරනවා, converter එක නොවෙයි |
| `physicalDevice` | ඇත්ත output/input pair එකක් — **එකම hardware-level pass** එක |

### Tests වලින් pin කරලා තියෙන නීති 3

1. **Simulated round trip එකක් කවදාවත් hardware කියලා report කරන්නේ නෑ.** Buffer එකක් buffer එකකට දුන්නා නම් ඒක software test එකක් — `simulated` කියලා තමයි label වෙන්නේ.
2. **Skipped එකක් කවදාවත් pass කියලා judge වෙන්නේ නෑ.** Device නැත්නම් උත්තරේ `skipped` — "හරි, හැම දෙයක්ම වැඩ" නෙවෙයි.
3. **වැරදි frequency එකකින් හෝ tolerance එකෙන් පිට level එකකින් ආපු tone එකක් fail.** Magic number එකක් නෙවෙයි, `RoundTripTolerance` object එකක්.

දැනට device round trip එක **`skipped`** return කරනවා — capture එක drive කරන්නේ නැති නිසා. ඒක තමයි ඇත්ත උත්තරය. Step C එකේදී ඒක `physicalDevice` වෙන්න ඕන.

---

## 8. Hardware vs software — පැහැදිලි වෙන් කිරීම

මේක මුළු වැඩේ ප්‍රධාන නීතිය. **Hardware test කරලා නැති දේ hardware test කළා කියලා කියන්නේ නෑ.**

| කාණ්ඩය | මොකද මේකට වැටෙන්නේ | තත්ත්වය |
|---|---|---|
| ✅ **Software verified** | DSP, mixer, clips, VST3 hosting, save/load, recorder (simulated input), measurements | කරලා, tests වලින් pin කරලා |
| ✅ **Real Windows, no sound card** | Windows device enumeration, ASIO compile | CI එකෙන් ඔප්පු වුණා |
| ❌ **Physical output** | Speaker එකකින් ඇහෙනවද, noise, latency | **test කරලා නෑ** |
| ❌ **Physical input** | Real mic/preamp එකකින් record | **test කරලා නෑ** |
| ❌ **Virtual device round trip** | VB-CABLE හරහා signal එක යනවද | **test කරලා නෑ** (install කරලා නෑ) |
| ❌ **ASIO** | Driver එකක් open වෙලා audio යනවද | **test කරලා නෑ** |
| ⚠️ **WASAPI loopback** | JUCE expose කරන්නේ නෑ | supported නෑ |

**කිසිම විටෙක:** driver එකක් install කිරීම = testing. VB-CABLE install කිරීම = speaker quality/mic noise/latency verify කිරීම. Device එකක් list එකේ පෙනීම = වැඩ කරනවා.

---

## 9. CI — මොකද ඇත්තටම ඔප්පු කරන්නේ

| Job | මොකද කරන්නේ | මොකද ඔප්පු කරන්නේ |
|---|---|---|
| Windows (MSVC, Release) | Build + ctest + ASIO compile check + device report | Windows එකේ compile වෙනවා, tests pass, ASIO backend binary එකට එනවා |
| Windows (MSVC, Debug + leak detector) | Build + ctest + JUCE leak check | **Leaks නෑ** (Release එකෙන් මේක අහුවෙන්නේ නෑ) |
| Linux (GCC, Release) | Build + ctest | Portable code, ASIO නැති platform එකේ වැඩ කරනවා |

### CI කරන්නේ **නැති** දේවල්

- Speaker එකකින් ශබ්දය ඇහෙනවද — hosted runner එකේ sound card එකක් නෑ
- ASIO driver එකක් වැඩ කරනවාද — driver එකක් නෑ
- Latency, glitches, clicks — real device එකක් ඕන
- Real microphone එකකින් recording

**ඒ නිසා:** CI green වීම = "code එක හරි, tests pass". CI green වීම ≠ "audio හරියට වැඩ කරනවා".

### අවසන් CI ප්‍රතිඵලය (run `42356228869`, commit `18650cd`)

| Job | ප්‍රතිඵලය |
|---|---|
| Windows Debug + leak detector | ✅ success |
| Windows Release | ✅ success |
| Linux Release | ✅ success |

`100% tests passed, 0 tests failed out of 15` — දෙපැත්තේම.

---

## 10. Windows එකෙන් ආපු ඇත්ත data

`windows-2022` runner එකේ `NUX DAW --device-report` (Release) — verbatim:

```
note: ASIO is compiled in but no ASIO driver is installed.
ASIO compiled in: yes
ASIO driver usable: no
Virtual cable present: no
Summary: DirectSound: 1 output, 1 input device(s).
Engine status: Error opening Primary Sound Driver: "No driver"
```

මේකෙන් පැහැදිලි වෙන දේවල් 2ක්:

**1. Device list එක සත්‍යය නෙවෙයි.** Sound card එකක්ම නැති machine එකක DirectSound කියනවා `1 output, 1 input` කියලා — මොකද ඒක "Primary Sound Driver" කියන phantom එකක් report කරනවා. NUX ඒක open කරන්න ගිහින් `No driver` කියලා හරි error එක දුන්නා. **Enumerate වීම = claim එකක්. Open වීම = සාක්ෂිය.** මේක තමයි Verification ladder එකෙන් කියන්නෙත්.

**2. ASIO compile වෙනවා, ඒත් කවදාවත් run වෙලා නෑ.** `ASIO compiled in: yes` = backend එක binary එකට එනවා. `ASIO driver usable: no` = ඇත්ත උත්තරය.

Linux runner එකේ එකම flag එක: `[ALSA] available = no, outputs = 0`, `ASIO compiled in: no`.

---

## 11. හම්බුණු bugs — සම්පූර්ණ ලැයිස්තුව

| # | Bug | හේතුව | ඇහුණේ කොහොමද |
|---|---|---|---|
| 1 | **CI කවදාවත් tests run කළේ නෑ** | `enable_testing()` නෑ → `ctest` tests 0ක් හොයාගෙන success කිව්වා | log එකේ `Test #` lines නොතිබීමෙන් |
| 2 | **Windows segfault** | `sizeof(AudioEngine)` = 434,576 (Linux) / **447,584 bytes (Windows)** stack එකේ. Windows stack **1 MB**, Linux **8 MB** → Windows එකේ පළමු engine test එකේම overflow | per-group ctest split එකෙන් crash වෙන group 3 නම් කළා |
| 3 | **`--device-report` කවදාවත් exit වුණේ නෑ** | quit වෙන්නේ `--screenshot` එක්ක විතරයි → window එක අරන් message loop එකේ ඉන්නවා | Windows Release job එක පැය භාගයක් එක step එකේ එල්ලිලා තිබුණා |
| 4 | **Device report එකේ content එක log එකට ආවේ නෑ** | app එක `std::cerr` එකට ලියනවා, workflow එක කියෙව්වේ stdout file එක විතරයි | step එක "success" ඒත් හිස් |
| 5 | **Loudness 0.73 LU වැරදි** | `−0.691` offset එක දෙපාරක් යෙදුණා (ඒක block formula එකේ තියෙනවා) | libebur128 එක්ක සංසන්දනය |
| 6 | **K-weighting approximate** | නිවැරදි filter එකක් නෙවෙයි approximation එකක් → −20.73 vs −20.00 | reference table |
| 7 | **`getMagnitudeAt` ~1.4 dB වැරදි** | nearest FFT bin එකට snap වුණා, parabolic interpolation නෑ | known-answer test |
| 8 | **Crosstalk absolute එකක් විදියට report වුණා** | ratio එකක් වෙන්න ඕන තැන level එකක් | known-answer test |
| 9 | **Mono generator heap overflow** | mono buffer එකේ ඉඩට වඩා ලිව්වා | ASAN-style guard test (`Mono signals stay inside their buffer`) |

### රටාවක් පේනවා

Bug 1, 3, 4 **එකම ආකාරයේ** — *"step එක success ඒත් ඇත්තටම කිසිම දේක් කළේ නෑ"*. Bug 1 ඒ නිසා Bug 2 ව (සහ අනිත් ඕනෑම Windows bug එකක්) **දිගටම වැහුවා**. ඒ නිසා දැන් CI එකේ රීතියක් තියෙනවා:

> Output එකේ `All NUX tests passed` නැත්නම් CI fail. Step එකකට deadline එකක් නැත්නම්, නැත්නම් step එකේ output එක හිස් නම් — ඒක success නෙවෙයි.

---

## 11b. අලුතින් එකතු වුණේ — device තේරීම + device round trip

| දේ | තත්ත්වය |
|---|---|
| Device ලැයිස්තුව, `[virtual]` label, output/input chooser | ✅ |
| Virtual cable එකේ දෙපැත්ත pair කිරීම (CABLE Input ↔ CABLE Output) | ✅ |
| වැරදි pair එකක් ප්‍රතික්ෂේප කිරීම (input නෑ / device නෑ / driver දෙකක්) | ✅ විස්තරයක් සමඟ |
| `AudioEngine::applyDeviceChoice()` — තෝරපු pair එකම open කිරීම | ✅ |
| Round trip runner (tone → output → input → trim → measure → judge) | ✅ ලියලා ඉවරයි |
| **ඇත්තට device එකක් හරහා round trip එකක් run කිරීම** | ❌ **කරලා නෑ** — SKIPPED |
| Tests | ✅ **13 groups**, ctest `NUX_device_selection` |

**මෙතන ඇත්ත:** runner එක වැඩ කරනවද කියලා device එකක් නැතිව කියන්න බැහැ. ඒ නිසා
report එකේ ඒක `🟡 built, never run with a device` කියලා තමයි තියෙන්නේ. VB-CABLE එකක්
install කළ Windows machine එකක ROUND TRIP button එක එබුවම ලැබෙන ප්‍රතිඵලය තමයි ඊළඟ
ඇත්ත දත්තය.

---

## 12. Known limitations (ඇත්තටම තියෙන ඒවා)

1. **Physical audio කිසිවක් test කරලා නෑ** — speaker, mic, interface, headphone.
2. **ASIO unverified** — install කරලත් නෑ, open කරලත් නෑ.
3. **VB-CABLE install කරලා නෑ** — virtual round trip `skipped`.
4. **WASAPI loopback නෑ** — JUCE එකේ `AUDCLNT_STREAMFLAGS_LOOPBACK` නෑ (0 hits).
5. **Measurements calibrated නෑ** — implemented + cross-checked, certified instrument එකක් එක්ක නෑ.
6. **Virtual device selection UI නෑ** — engine එක default device එක auto-open කරනවා (`initialise(2, 2, nullptr, true)`).
7. **Recording real mic එකකින් unverified.**
8. **Latency, CPU, glitches මැනලා නෑ** — hardware ඕන.
10. **Automation, MIDI learn, CPU metering, latency compensation, sidechain** — හදලා නෑ.
11. **JUCE AGPLv3-or-commercial** තීරණය ගන්න ඕන; "NUX" trademark clearance නෑ.

---

## 13. Virtual Audio Lab — පියවර 4

Hardware එක අන්තිමට තියලා, ඒකට කලින් verify කරන්න පුළුවන් හැම දෙයක්ම කරන පිළිවෙල:

| Step | මොකද | තත්ත්වය |
|---|---|---|
| Offline tests | device නැතිව DSP/mixer/clips/recorder (simulated) | ✅ **done** |
| **Step A** — measurement maths | levels, THD, SNR, true peak, BS.1770, spectrum + known-answer tests | ✅ **done** (20 groups, bugs 5ක් හම්බුණා) |
| **Step B** — round trip + verification levels | generate → (device) → capture → measure → compare; `Verification` ladder | ✅ **software round trip done** (10 groups); device round trip `skipped` |
| **Step C** — device round trip | Runner එක ලියලා ඉවරයි; **device එකක් එක්ක run කරලා නෑ** | 🟡 hardware ඕන |
| **Step D** — ASIO | real driver එකක්, ඇත්තට open කරලා | ⏳ **අන්තිමට** |

### Step C සඳහා acceptance criteria (කලින්ම ලියලා තියෙන්නේ)

1. Real output/input pair එකක් select කරලා round trip එකක් run කරන්න ඕන.
2. ප්‍රතිඵලය `physicalDevice` level එකෙන් report වෙන්න ඕන — `simulated` නෙවෙයි.
3. Frequency drift, level error, THD+N, latency එක මැනලා `AUDIO_TEST_REPORT.md` එකට ලියන්න ඕන.
4. Fail වුණොත් **fail කියලා** ලියන්න ඕන. Pass කියලා ලියන්නේ මැනපු දේ ඇත්ත නම් විතරයි.
5. `docs/hardware-checklist.md` එකේ හැම පේළියක්ම කරලා, result එක සමඟ.

### Step D සඳහා acceptance criteria

1. ASIO driver එකක් detect වෙන්න ඕන (ASIO4ALL හෝ interface එකේ own driver).
2. **ඇත්තට open වෙලා** audio යන්න ඕන — driver එක install වීම ප්‍රමාණවත් නෑ.
3. එතකන් `AUDIO_TEST_REPORT.md` එකේ `ASIO driver usable: no` කියලා තමයි තියෙන්නේ.

---

## 14. Build කරලා run කරන විදිය

### Windows (Visual Studio 2022)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
build\NUX_artefacts\Release\NUX DAW.exe
```

### Linux

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target NUXTests -j1
ctest --test-dir build --output-on-failure
```

### Device inventory එක විතරක් බලන්න

```bash
"build/NUX_artefacts/Release/NUX DAW" --device-report
```

Window එකක් අරින්නේ නෑ — print කරලා exit වෙනවා (0.45 s).

---

## 15. ලේඛන map එක

| ලේඛනය | මොකටද |
|---|---|
| `README.md` | Project intro, feature table, build |
| `BUILDING.md` | Build instructions |
| `CHANGELOG.md` | Version එකකින් version එකට වෙනස්කම් |
| `AUDIO_TEST_REPORT.md` | **Audio testing brief එකේ නිල වාර්තාව** — passed / failed / not performed / simulated / hardware |
| `docs/AUDIO_MEASUREMENT_METHODS.md` | Measurement එකින් එකේ method + tolerance + reference values |
| `docs/hardware-checklist.md` | Step C එකේදී අතින් කරන්න ඕන දේවල් |
| `TODO-LIST.md` | ඉතුරු වැඩ, code එකේ ඇත්ත තත්ත්වය අනුව (Sinhala) |
| `PROJECT-STATUS.md` | **මේක** — හැම දෙයක්ම එක තැනකින් |

---

## 16. Security සහ licensing

| කාරණය | තත්ත්වය |
|---|---|
| Repo visibility | **private** ✅ |
| Token | Chat එකේ paste කරපු classic PAT එකේ broad scopes තියෙනවා (`repo`, `delete_repo`, `admin:org`, `workflow`, …) — **ඒවා හැම repo එකකටම බලපානවා**. Revoke කරලා fine-grained, repo-scoped, expiring token එකක් හදන්න ඕන |
| Workspace එකේ token | `/tmp/tok.txt` (mode 600) — snapshot එකට යන්නේ නෑ, ඒත් turn එකකට වරක් නැති වෙනවා |
| JUCE licence | AGPLv3 හෝ commercial — තීරණයක් ගන්න ඕන |
| "NUX" trademark | Clearance කරලා නෑ |

---

## 17. ඊළඟට කරන්න ඕන (පිළිවෙලට)

| # | කාර්යය | කාටද |
|---|---|---|
| 1 | PR #3 → #4 merge order එක විසඳලා merge කරන්න | ඔයා |
| 2 | Token rotate කරන්න (fine-grained) | ඔයා |
| 3 | **Step C** — speaker + mic එකක් තියෙන Windows PC එකක round trip | hardware ඕන |
| 4 | Virtual device selection UI (TODO 1.3) — hardware නැතිව කරන්න පුළුවන් | මම |
| 5 | ✅ ~~`beginTest` 115 vs 114~~ විසඳුණා — `PluginHostTests` එකේ group එකක් plugins **නැති විට** විතරයි run වෙන්නේ | ✅ |
| 6 | **Step D** — ASIO, real driver එකක් එක්ක | අන්තිමට |
| 7 | Automation / MIDI learn — ඊළඟ phase එකේ වැඩ | පස්සේ |

---

## 18. අවසාන වචනය

මේ වැඩේ අරමුණ "test pass කරගන්න එක" නෙවෙයි — **ඇත්ත දැනගන්න එක**. ඒ නිසා:

- Physical hardware test කරලා නැති දේ **test කළා කියලා කියන්නේ නෑ**. Report එකේ ඒක `NOT PERFORMED` කියලා තමයි තියෙන්නේ.
- Simulated test එකක් **simulated** කියලා තමයි label වෙන්නේ — හැම ප්‍රතිඵලයකම verification level එකක් තියෙනවා.
- CI green වීම **code එක හරි** කියන එක විතරයි — audio හරියට යනවා කියන එක නෙවෙයි.
- හම්බුණු bugs 9ම ලේඛනගත කරලා තියෙනවා — ඒවා හංගන්නේ නෑ, මොකද ඒවා හංගන තැන තමයි ඊළඟ එක හැංගෙන්නේ.

Hardware එකක් ලැබුණ ගමන් **Step C** එකෙන් ඇත්ත hardware verification එක පටන් ගන්න පුළුවන් — checklist එකයි, acceptance criteria එකයි දැනටමත් ලියලා ඉවරයි.
