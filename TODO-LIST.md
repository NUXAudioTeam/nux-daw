# NUX DAW — ඉතුරු වැඩ ලැයිස්තුව

> **යාවත්කාලීනය:** 2026-11-07 · branch `feature/audio-testing` · `02f64b6`
> **තත්වය:** 127 groups pass, ctest **16/16** (Linux Release). Device selection + virtual loopback runner එකතු විය.

මේක **කේතයේ ඇත්ත තත්වය** බලලා හදපු ලැයිස්තුවක්. මම හැම පේළියක්ම code එකේ බලලා තමයි ලියන්නේ.
"✅" = compile වෙලා, test වෙලා, වැඩ කරනවා. "⬜" = නෑ. "🟡" = තියෙනවා නමුත් අසම්පූර්ණ.

---

## 🔴 0. දැන් අතේ තියෙන වැඩ — feature/audio-testing

| # | කාර්යය | සටහන | තත්ත්වය |
|---|---|---|---|
| 0.1 | **Windows CI segfault එක හොයාගන්න** | ✅ **විසඳා ඇත.** හේතුව logic bug එකක් නෙවෙයි — `sizeof(AudioEngine)` = **447,584 bytes (Windows)**. Tests stack එකේ `AudioEngine engine;` හැදුවා → Windows stack 1 MB → overflow. Linux 8 MB නිසා පෙනුණේ නෑ. Heap එකට මාරු කළා + size guard test එකක් | ✅ |
| 0.2 | ~~`feature/audio-testing` PR~~ | ✅ **PR #4** ඇරියා. දැන් `feature/engine-hardening` වෙනම branch + PR #5 | ✅ |
| 0.3 | Merge order එක විසඳන්න | #1 → #2 → #3 → #4 → #5. **ඔයාගේ පැත්තෙන්** — මට merge කරන්න බෑ | ⏳ |
| 0.4 | ~~`beginTest` 115 vs report 114~~ | ✅ **විසඳුණා — bug එකක් නෙවෙයි.** `PluginHostTests` එකේ group එකක් තියෙන්නේ plugins **නැති විට** විතරයි. Plugins build කළොත් 8 real groups run වෙනවා, එකක් අඩුවෙන් පේනවා. Design එකෙන් එහෙමයි | ✅ |

---

## 🟠 1. Audio Engine — brief එකේ §1–§6 (මූලික වැඩ ඉවර, ඉතුරු ටික)

| # | කාර්යය | දැන් තත්ත්වය |
|---|---|---|
| 1.1 | Offline DSP testing (440 Hz, levels, clipping, ramps, stereo, block sizes, sample rates) | ✅ 13 groups |
| 1.2 | Device detection + VB-CABLE හඳුනාගැනීම + error messages | ✅ 6 groups |
| 1.3 | **Virtual device එකක් UI එකෙන් තෝරන්න** | ✅ **අවසන්.** AUDIO SETTINGS එකේ VIRTUAL AUDIO LAB block එක: output/input chooser, `[virtual]` label, OPEN PAIR (engine එකට apply), ROUND TRIP. `AudioEngine::applyDeviceChoice()` |
| 1.4 | Recording — simulated input, WAV writing, integrity | ✅ (simulated) |
| 1.5 | Recording — **real mic** එකෙන් | ❌ hardware නැහැ — unverified |
| 1.6 | ASIO detect + compile | ✅ Windows CI එකේ compile වෙනවා (proven) |
| 1.7 | **ASIO driver එකක් ඇත්තට open කරලා test** | ❌ කවදාවත් කරලා නෑ → "unverified" කියලා තමයි තියෙන්න ඕන |
| 1.8 | WASAPI loopback capture | ❌ JUCE එකේ නෑ (`AUDCLNT_STREAMFLAGS_LOOPBACK` → 0 hits). Not supported |

### Virtual Audio Lab (ඔයා එකඟ වුණ පිළිවෙල)

| # | පියවර | තත්ත්වය |
|---|---|---|
| 1.9 | **Step A — measurement maths + known-answer tests** | ✅ **අවසන්** (`99c12ed`) — 20 groups, bugs 5ක් හම්බුණා |
| 1.10 | **Step B — virtual device round-trip**: generate → device → capture → measure → compare | ✅ **software 10 groups** + ✅ **device runner එක ලියලා ඉවරයි** (13 groups, `VirtualLoopback.cpp`). ඒත් **device එකක් එක්ක කවදාවත් run කරලා නෑ** → SKIPPED |
| 1.11 | Step C — real speaker/mic on Windows PC | ⬜ ඔයාගේ hardware එක ඕන |
| 1.12 | Step D — ASIO (අන්තිමට) | ⬜ |

---

## 🟠 2. Stubs — UI එකේ තියෙනවා, logic එක නෑ

මේවා මම "implemented" කියලා report කරලා නෑ:

| # | කාර්යය | දැන් තත්ත්වය | කරන්න ඕන |
|---|---|---|---|
| 2.1 | ~~Record button UI එකට wire කිරීම~~ | ✅ **දැනටමත් තියෙනවා — මගේ කලින් සටහන වැරදියි.** `TransportBar::recordButton` → `onRecordToggled` → `MainComponent::toggleRecording()` → `engine.startInputRecording()` → `stopInputRecording()` → `placeRecordedTake()`. `--record-self-test` එකෙන් ඔප්පු කළා (take → clip, track → audio, clip load වුණා). **ඉතුරු:** latency compensation + monitoring | ✅ (latency ❌) |
| 2.2 | **Piano roll velocity knob** | `ignoreUnused` කරලා | note draw වෙද්දී value එක use කරන්න |
| 2.3 | **Octave +/- buttons** | `octaveOffset` විතරයි වෙනස් වෙන්නේ, canvas scroll වෙන්නේ නෑ | `setLowestNote()` hook |
| 2.4 | **Pattern clips (multi-clip)** | Model එකේ `Clip` array, UI එකේ එකම clip එකයි edit වෙන්නේ | timeline blocks, add/delete/move |
| 2.5 | **Track drag-to-reorder** | Right-click menu එකේ delete තියෙනවා, drag නෑ | drag & drop ordering |
| 2.6 | **Metronome** | icon එක තියෙනවා, click track නෑ | audio click generator |
| 2.7 | **MIDI recording → timeline** | Notes capture වෙනවා (arm කරලා), timeline එකට ලියන්නේ නෑ | quantize + write to clip |
| 2.8 | **Multi-plugin chain UI** | `PluginSlot` එකක් process කරනවා, chain UI නෑ | slot එකට plugin කිහිපයක් |
| 2.9 | **Instrument එක track header එකෙන් තෝරන menu** | Engine API එකෙන් විතරයි | menu එකක් |

---

## 🟡 3. MIDI & editing — v0.4

| # | කාර්යය | සටහන |
|---|---|---|
| 3.1 | Note length / resize | දැන් හැම note එකම 1 step |
| 3.2 | Multi-select notes | marquee, copy/paste/duplicate |
| 3.3 | Velocity lane | piano roll එකේ පහළින් bars |
| 3.4 | Quantize / swing | groove controls |
| 3.5 | MIDI learn / CC mapping | knobs → hardware controllers |
| 3.6 | Time signature / multiple bars | දැන් 4/4 fix |
| 3.7 | Arrangement / song mode | `patternLengthSteps` විතරයි |
| 3.8 | Clip trim / split / move | audio clip එකේ ඉතුරු |
| 3.9 | Real peak cache (waveform) | දැන් simplified |

---

## 🟢 4. Pro features — v1.0

| # | කාර්යය |
|---|---|
| 4.1 | Automation lanes (volume, pan, plugin params) |
| 4.2 | Bus routing matrix (bus → bus, sidechain) |
| 4.3 | Freeze / bounce track |
| 4.4 | Stem export (per-track WAV) |
| 4.5 | MP3 / FLAC export (JUCE license check ඕන) |
| 4.6 | Project templates + autosave / crash recovery |
| 4.7 | Drag & drop (files → timeline, clips between tracks) |
| 4.8 | HiDPI / scaling + accessibility |
| 4.9 | Installer (Inno Setup / WiX) + code signing |
| 4.10 | Crash reporting / logging |
| 4.11 | CPU metering, latency compensation |

---

## ⚙️ 5. තාක්ෂණික ණය — දැනුවත්ව තියලා ගිය දේවල්

| # | ප්‍රශ්නය | ඇයි අවුල් | විසඳුම |
|---|---|---|---|
| 5.1 | ~~Insert params thread safety~~ | ✅ **විසඳුණා + TSan එකෙන් verify කළා.** `AtomicInsertState` publish/acquire. TSan යටතේ groups 131, races **0**. ඒ ගමන් **MIDI thread එකේ තව race එකක්** හම්බුණා (`recordArmed` model එකෙන් කියවනවා) → හදලා regression test එකක් දැම්මා | ✅ |
| 5.2 | **`TrackState` එකේ GUI dependency** | `Colour` සඳහා `juce_graphics` include | ARGB `uint32`, conversion UI layer එකේ |
| 5.3 | **ValueTree එකක් නෑ** | Plain structs + manual `markDirty()`. Undo වැඩ කරනවා නමුත් automation එකට අවහිරයි | `ValueTree` + `UndoManager` migrate |
| 5.4 | **`-Wfloat-equal` warnings** | `InsertSlot::operator==` | `juce::exactlyEqual` |
| 5.5 | **Deprecated `AlertWindow`** | `showYesNoCancelBox` (JUCE 9) | `showScopedAsync()` |
| 5.6 | **`JUCE_MODAL_LOOPS_PERMITTED=1`** | Modal loops සඳහා | Async API එකට ගිහින් අයින් කරන්න |
| 5.7 | **Solo double-mute** | `tracksSoloed` + `strip.muted` දෙකෙන්ම | එකම තැනකට |
| 5.8 | **Engine test ALSA warning** | Headless MIDI device නැති නිසා | test එකේ MIDI input disable |

---

## ⚠️ 6. Release blockers — මගේ පැත්තෙන් කරන්න බැරි

| # | කාර්යය | තත්ත්වය |
|---|---|---|
| 6.1 | **GitHub PAT එක revoke කරන්න** | ⏳ ඔයාගේ පැත්තෙන් — token එක chat එකේ public වුණා |
| 6.2 | **Real Windows PC එකේ manual test** | ⬜ device enumeration, latency, real VST3, real mic |
| 6.3 | **"NUX" trademark clearance** | ⬜ |
| 6.4 | **JUCE license** | ⬜ AGPLv3 හෝ commercial — තීරණයක් ඕන |

---

## 📋 මගේ නිර්දේශිත අනුපිළිවෙල

1. **0.1** Windows segfault — CI run එකේ result එක බලලා. මේක නැතුව "green" කියන්න බෑ.
2. **0.2 + 0.3** PR එක අරින්න, merge order එක විසඳන්න.
3. **1.10** Virtual Audio Lab Step B — virtual device round-trip. Step A එකේ measurements දැන් ready, මේක තමයි ඊළඟ අර්ථවත් පියවර.
4. **1.3** Virtual device selection UI — brief එකේ ඉල්ලලා තියෙන දේ, detect කරලා තියෙනවා නමුත් තෝරන්න බෑ.
5. **2.1** Record button wire කිරීම — recording engine එක තියෙනවා, UI එකෙන් ළඟා කරන්න බෑ.
6. **5.1, 5.3** technical debt — මේක පස්සේ කරන්න බැහැ, එහෙම නොකළොත් automation එක double ලියන්න වෙනවා.
7. **3.x** MIDI editing → **4.x** pro features.

**Hardware සහ software වෙන් කරලා තමයි මම report කරන්නේ.** 1.5, 1.7, 1.11, 6.2 — මේවා හැම එකක්ම hardware එකක් ඕන, ඒ නිසා "not performed" විදියටම තියෙනවා.
