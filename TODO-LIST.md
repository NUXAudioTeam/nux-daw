# NUX DAW — මොකද ඉතුරු, මොකද කරන්න ඕන

> අවසන් යාවත්කාලීනය: v0.3.0 foundation (branch `feature/v0.3-foundation`)

මේක **ඇත්තටම කේතයේ තත්වය** අනුව හදපු ලැයිස්තුවක්. "implemented" කියලා ලියලා තියෙන දේවල් compile වෙලා test වෙලා තියෙනවා; "stub" කියලා තියෙන දේවල් UI එකේ තියෙනවා නමුත් ඇත්තටම වැඩ කරන්නේ නැහැ.

---

## 🔴 1. අනිවාර්යයෙන් මුලින්ම — Release blockers

| # | කාර්යය | ඇයි වැදගත් | තත්වය |
|---|---|---|---|
| 1.1 | **GitHub token එක revoke කරන්න** | Token එක chat එකේ public වුණා. Repo 30කටම access තියෙනවා | ⏳ ඔයාගේ පැත්තෙන් |
| 1.2 | `.gitignore` + `build/` ඉවත් කරලා commit | දැන් repo එකේ commit එකක්වත් නැහැ, `build/` (136 MB binary) stage වෙලා තියෙනවා | ⬜ |
| 1.3 | `feature/v0.3-foundation` branch + **Pull Request** | ඔයාගේ prompt එකේ තියෙන rule එක (main එකට කෙලින්ම යවන්නේ නැහැ) | ⬜ |
| 1.4 | **Windows/MSVC build එක verify කරන්න** | මම build කළේ Linux/GCC එකෙන් විතරයි. VS 2022 එකේ compile වෙනවද කියලා තාම ඔප්පු කරලා නැහැ | ⬜ |
| 1.5 | GitHub Actions CI (`windows-latest`) | ඉහත 1.4 ස්වයංක්‍රීයව හැම PR එකකදීම verify වෙන්න | ⬜ |
| 1.6 | JUCE assertion 2ක් හදන්න | `grabKeyboardFocus()` window එක show වෙන්න කලින්; timer එකකින් repaint | ⬜ |
| 1.7 | `README.md`, `BUILDING.md`, `LICENSE`, `CHANGELOG.md` | නැතුව project එකක් "proper" වෙන්නේ නැහැ | ⬜ |

---

## 🟠 2. v0.3 එකේ අඩුපාඩු — UI තියෙනවා, logic එක නැහැ (stubs)

මේවා දැන් **ඇත්තටම වැඩ කරන්නේ නැහැ**. මම මේවා "implemented" කියලා report කළේ නැහැ:

| # | කාර්යය | දැන් තත්වය | කරන්න ඕන දේ |
|---|---|---|---|
| 2.1 | **Audio clip playback** | `AudioClip` data model + `.nux` save/load තියෙනවා. නමුත් engine එකේ **playback code එකක්ම නැහැ** | `AudioFormatManager` + `AudioTransportSource` හෝ pre-loaded buffer, timeline එකට sync කරලා stream කරන්න |
| 2.2 | **Audio recording** | `startRecordingToFile()` engine එකේ තියෙනවා, නමුත් UI එකේ button එකකට **wire කරලා නැහැ** | Record button එක + input channel + latency compensation |
| 2.3 | **VST3 plugin එක signal path එකට** | Scan කරනවා ✅, editor එක open වෙනවා ✅. නමුත් plugin එකේ **audio processing එක signal chain එකට වැටෙන්නේ නැහැ** | `AudioPluginInstance` එක `InsertProcessor` එකකට wrap කරලා process කරන්න |
| 2.4 | **VST3 instrument track** | Instrument plugins load වෙනවා, නමුත් built-in synth එක විතරයි note ලබාගන්නේ | Plugin instrument එකකට MIDI route කරන්න |
| 2.5 | **Piano roll velocity knob** | UI එකේ තියෙනවා, `ignoreUnused` කරලා තියෙන්නේ | Note එකක් draw කරද්දී ඒ value එක use කරන්න + note එකේ velocity edit කරන්න |
| 2.6 | **Octave +/- buttons** | `octaveOffset` විතරයි වෙනස් වෙන්නේ, canvas එකේ view එක scroll වෙන්නේ නැහැ | `setLowestNote()` එකට hook කරන්න |
| 2.7 | **Pattern clips (multi-clip)** | Model එකේ `Clip` array එකක් තියෙනවා, නමුත් UI එකේ **එකම clip එක** විතරයි edit වෙන්නේ | Timeline එකේ clip blocks, add/delete/move |
| 2.8 | **Insert parameter editing** | Insert එකක් add කරන්න පුළුවන්, නමුත් **knob එකකින් value වෙනස් කරන්න බැහැ** | Insert එක click කරද්දී parameter panel එකක් |
| 2.9 | **Track reorder / delete UI** | Right-click menu එකේ තියෙනවා, නමුත් drag-to-reorder නැහැ | Drag & drop track ordering |
| 2.10 | **Loop length vs pattern length** | `patternLengthSteps` විතරයි; song mode එකක් නැහැ | Arrangement/song mode (9 වෙනි කොටස බලන්න) |

---

## 🟡 3. v0.4 සඳහා — MIDI & editing (roadmap එකේ ඊළඟ පියවර)

| # | කාර්යය | සටහන |
|---|---|---|
| 3.1 | **Undo / Redo** | දැන් නැහැ. `UndoManager` + `ValueTree` එකට migrate කරන්න ඕන (මේක ලොකු refactor එකක්) |
| 3.2 | **Note length / resize** | Notes හැම වෙලාවෙම 1 step. Drag-to-resize + length field |
| 3.3 | **Multi-select notes** | Marquee selection, copy/paste/duplicate |
| 3.4 | **Velocity lane** | Piano roll එකේ පහළින් velocity bars |
| 3.5 | **Quantize / swing** | Groove controls |
| 3.6 | **MIDI recording** | External keyboard එකෙන් notes capture වෙනවා ✅ (arm කරලා), නමුත් **timeline එකට ලියන්නේ නැහැ** |
| 3.7 | **MIDI learn / CC mapping** | Knobs → hardware controllers |
| 3.8 | **Metronome** | Icon එක තියෙනවා, click track එක නැහැ |
| 3.9 | **Time signature / multiple bars** | දැන් 4/4 fix වෙලා |

---

## 🟢 4. v1.0 සඳහා — Pro features

| # | කාර්යය |
|---|---|
| 4.1 | **Automation lanes** (volume, pan, plugin parameters) |
| 4.2 | **Waveform display** for audio clips (`AudioThumbnail`) |
| 4.3 | **Bus routing matrix** (bus → bus, sidechain) |
| 4.4 | **ASIO support verify** — JUCE එකේ තියෙනවා, නමුත් SDK path + license check ඕන |
| 4.5 | **Freeze / bounce track** to save CPU |
| 4.6 | **Stem export** (per-track WAV) |
| 4.7 | **MP3 / FLAC export** (JUCE එකේ license issue — check කරන්න) |
| 4.8 | **Project templates + autosave / crash recovery** |
| 4.9 | **Drag & drop** (files into timeline, clips between tracks) |
| 4.10 | **HiDPI / scaling** + accessibility |
| 4.11 | **Installer** (Inno Setup / WiX) + code signing |
| 4.12 | **Crash reporting / logging** |

---

## ⚙️ 5. තාක්ෂණික ණය (technical debt) — මම දැනුවත්ව තියලා ගිය දේවල්

| # | ප්‍රශ්නය | ඇයි දැන් එහෙම කළේ | විසඳුම |
|---|---|---|---|
| 5.1 | **Solo logic double-mute** | Solo එක `tracksSoloed` atomic එකෙන් **සහ** `strip.muted` එකෙන් handle වෙනවා. දැන් වැඩ කරනවා, නමුත් අතිරික්තයි | Solo/mute එකම තැනකට ගේන්න |
| 5.2 | **Insert params thread safety** | Audio thread එක `inserts[]` struct එක read කරනවා, UI එක ලියනවා (audio callback එක නවත්තලා නැති වෙලාවෙ) | `std::atomic<float>` array එකකට හෝ lock-free command queue එකකට |
| 5.3 | **`TrackState` එකේ GUI dependency** | `Colour` සඳහා `juce_graphics` include කරලා (headless tests සඳහා අවශ්‍ය වුණා) | ARGB `uint32` එකක් රඳවලා, conversion එක UI layer එකට |
| 5.4 | **ValueTree එකක් නැහැ** | Plain structs + manual `markDirty()`. Undo/redo එකට මේක අවහිරයි | JUCE `ValueTree` + `UndoManager` එකට migrate |
| 5.5 | **`-Wfloat-equal` warnings** | `InsertSlot::operator==` එකේ `p1 == o.p1` | `juce::exactlyEqual` use කරන්න |
| 5.6 | **Deprecated `AlertWindow` API** | `showYesNoCancelBox` (JUCE 9 deprecated) | `AlertWindow::showScopedAsync()` එකට මාරු කරන්න |
| 5.7 | **`JUCE_MODAL_LOOPS_PERMITTED=1`** | `AlertWindow` modal loops සඳහා | Async API එකට ගිහින් මේක අයින් කරන්න |
| 5.8 | **Tests: Linux විතරයි** | Windows CI එකක් තාම නැහැ | 1.5 CI workflow එකෙන් විසඳෙනවා |
| 5.9 | **Engine test එකේ ALSA warning** | Headless environment එකේ MIDI device නැති නිසා | Test එකේ MIDI input disable කරන්න |

---

## 📋 මගේ නිර්දේශිත අනුපිළිවෙල

**පියවර 1 (අද):** 1.1 – 1.7 → clean repo, PR, CI, Windows verify, docs
**පියවර 2:** 2.1, 2.3, 2.8 → **audio clips + VST3 in signal path + insert params** (මේ තුන නැතුව DAW එකක් "DAW" වෙන්නේ නැහැ)
**පියවර 3:** 5.4, 3.1 → ValueTree + Undo (මේක පස්සේ කරන්න බැහැ, එහෙම නොකළොත් හැම feature එකක්ම දෙපාරක් ලියන්න වෙනවා)
**පියවර 4:** 2.2, 3.2–3.8 → recording + MIDI editing
**පියවර 5:** 4.x → pro features

---

### මොකද මම ඊළඟට කරන්න ඕන?
ඔයා කැමති නම් මම **පියවර 1** (repo + PR + CI + docs + assertion fixes) දැන්ම කරන්නම් — ඒකෙන් පස්සේ ඔයාට Windows එකේ clone කරලා build කරලා බලන්න පුළුවන්. නැත්නම් **පියවර 2** (audio clips / VST3 processing) එකෙන් පටන් ගන්නද?
