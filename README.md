# SoftVoice SAPI 5

The 1997 **SoftVoice** synthesiser exposed to Windows as **forty SAPI 5
voices** — twenty built-in personalities, each speaking English and Spanish —
usable by any speech application, in both 32-bit and 64-bit programs.

[**Download the installer →**](https://github.com/joshknnd1982/softvoice-sapi5/releases/latest)

---

## What SoftVoice is

SoftVoice is a formant synthesiser written by SoftVoice, Inc. and licensed to
The Productivity Works, who shipped it as the speech engine inside
**pwWebSpeak** — a self-voicing web browser from the late 1990s, and one of the
first browsers designed from the start for blind users rather than retrofitted
with a screen reader.

It is not a concatenative or neural voice and does not try to sound like a
person. It builds speech from a model of the vocal tract, which is why it can
offer twenty genuinely different characters — a giant, a martian, a fly, a
choir boy — out of one engine, and why it stays intelligible at speeds where
recorded-unit voices fall apart. For a lot of long-time screen reader users
that trade is the point.

The engine survives today because the
[svWebspeak](https://github.com/seedy60/svWebspeak) NVDA add-on keeps it
running. This project takes the same engine and makes it available to
*everything* on Windows that speaks, rather than to NVDA alone.

## The voices

| | | | |
|---|---|---|---|
| Male | Female | Large Male | Child |
| Giant Male | Mellow Female | Mellow Male | Crisp Male |
| The Fly | Robotoid | Martian | Colossus |
| Fast Fred | Old Woman | Munchkin | Troll |
| Nerd | Milktoast | Tipsy | Choir Boy |

Each appears twice: `SoftVoice Male` speaks English, `SoftVoice Male (Spanish)`
speaks Spanish. Those are the only two languages the engine has — there is no
German rule set in this package. Forty voices in all, and the installer lets you
choose exactly which of them to have — see below.

Hear all forty: the **samples archive** on the
[Releases page](https://github.com/joshknnd1982/softvoice-sapi5/releases/latest)
contains one file per voice plus a guided tour of each language. Or regenerate
them yourself with `python tools\render_samples.py`.

## Installing

Download `SoftVoiceSAPI5_Setup_<version>.exe` from
[Releases](https://github.com/joshknnd1982/softvoice-sapi5/releases/latest) and
run it. It needs administrator rights, because speech engines are registered
for the whole machine rather than for one user.

It installs:

- both SAPI 5 interfaces, 32-bit and 64-bit, and registers them
- the engine and its host
- the voices you chose, out of the forty on offer
- the **SoftVoice Speech Settings** utility, with a desktop shortcut
- optionally, the diagnostic tools

Then pick a SoftVoice voice in your screen reader or in Windows speech
settings. To remove it, use Apps and Features as normal.

### Choosing which voices to install

Forty voices in the list is a lot to page through if you only ever wanted two,
so the components page lets you pick them one at a time. Every voice is its own
check box, grouped under **English voices** and **Spanish voices**: check a
language to take all twenty of it, or open the group and choose individually.
Five ready-made choices sit above the tree — pick one and adjust it, or start
from **Custom**:

| Type | What you get |
|---|---|
| Everything | all forty voices, both languages, and the diagnostic tools |
| The everyday voices | the eight natural-sounding personalities, in English |
| English only | all twenty personalities, speaking English |
| Spanish only | all twenty personalities, speaking Spanish |
| Custom | pick the languages and the voices one at a time |

Choosing few costs nothing and gains nothing but a shorter list: the catalogue
is compiled into the DLL, so no voice takes disk space of its own. What the
choice really controls is which tokens the enumerator hands SAPI, and that is
recorded in `voices.ini` beside the program — one line per voice, `1` to publish
it and `0` to hide it. Change a line by hand (the folder is protected, so with
an elevated editor) and the next application to ask Windows for its voices sees
the change; nothing needs restarting. Deleting the file, or setting every line
to `0`, publishes everything again rather than leaving the machine silent.

Running the installer again is the easier way round: it opens on whatever was
chosen last time, so voices can be added or dropped without uninstalling first.

The one part of the choice that is a real file is Spanish itself. `Svspan32.dll`
is the engine's Spanish letter-to-sound rule set, and it is only laid down when
you keep at least one Spanish voice — and removed again if you later drop them
all.

At least one voice has to be chosen; the wizard refuses to go on otherwise,
since the alternative is a speech engine Windows can find and cannot use.

### Accessibility

The installer is built to be usable by the people most likely to want these
voices: every page is standard Win32 controls that screen readers read
natively, nothing steals focus, and no page auto-advances. The forty voices are
check boxes in the standard components tree rather than a custom page, so each
is announced with its name and its check box role as it is focused, and each
carries its language in the same form the voice itself uses — `Male`,
`Male (Spanish)` — so an item read on its own is unambiguous. The final page
states in text exactly what happened: which interfaces registered, how many
voices of each language are published, and where the logs are.

## The configuration utility

`SoftVoiceConfig.exe` adjusts, per voice, every parameter the engine exposes:

| Setting | Engine call | Range |
|---|---|---|
| Rate | `SVSetRate` | 0–100, 50 = **this voice's own speed** |
| Pitch | `SVSetPitch` | 0–100, 50 = **this voice's own pitch** |
| Volume | `SVSetVolume` | 0–100, 100 = the preset's own level |
| Inflection | `SVSetF0Range` | the voice's own, or 0–100 |
| Breathiness | `SVSetAHBias` | the voice's own, or 0–100 |
| Roughness | `SVSetF0Perturb` | the voice's own, or 0–100 |
| Vowel length | `SVSetVowelFactor` | the voice's own, or 0–100 |
| Glottal source | `SVSetGlottalSource` | the voice's own, or 8 named sources |
| Intonation | `SVSetF0Style` | the voice's own, normal, monotone, expressive |
| Voicing mode | `SVSetVoicingMode` | the voice's own, normal, soft, whispered |
| Gender | `SVSetGender` | the voice's own, male, female, neutral |
| Voicing amplitude | `SVSetAVBias` | the voice's own, or 0 to −60 |
| Volume makeup | host-side | automatic, or 100–600% |
| Sample rate | `SVOpenSpeech` | 8000, 11025 or 22050 Hz |

Everything below volume defaults to **the voice's own** and is sent only when
deliberately chosen. That is not tidiness. Each of the twenty personalities is
a complete preset, and there is no value for these that means "neutral":
forcing `SVSetAVBias(0)`, which looks exactly like a neutral, drives 14 of the
20 voices past full scale, and the engine wraps rather than saturating, so it
is heard as a harsh crackle. Rate and pitch are anchored per voice for the same
reason — preset rates span more than three to one, from Fast Fred at 301 to
Choir Boy at 89.

A change is written to the settings file the moment it is made, and the engine
re-reads that file at the top of every utterance, so it takes effect on the
next thing spoken with nothing restarted, and closing the utility cannot lose
it. Settings live in `%APPDATA%\SoftVoice SAPI5\softvoice.ini` — deliberately
not the registry, so the engine can speak without reading a registry key and
the utility needs no elevation to save.

### Accessibility

A plain Win32 dialog. Every control is in the tab order, none is ever disabled,
each has its own label immediately before it, and the access keys are unique.

Enumerated settings are drop-down lists rather than trackbars, because the MSAA
proxy for a trackbar reports its value as a *percentage of its range* — a list
of eight glottal sources sitting on the third would be announced as "28", a
number that appears nowhere in the documentation.

`tools\check_config_a11y.py` verifies all of this through MSAA, the same
interface NVDA uses, and fails if any focusable control is unnamed, disabled,
or shares an access key.

## How it works

The engine is a 32-bit 1997 DLL that creates a top-level window and is not
thread-safe, so it runs in its own process (`svwebspeak-host.exe`) and streams
PCM back over a loopback socket.

That single decision is why this package is simpler than most
dual-architecture speech wrappers:

- **One set of sources for both architectures.** Neither the 32-bit nor the
  64-bit interface loads the engine, so there is no bitness-specific code path,
  no COM surrogate, and no second helper to keep in step.
- **No SAPI 4.** The engine's own C entry points are called directly.
- **Nothing about the engine in the registry.** The host and `SVctl32.DLL` are
  patched to hand the SoftVoice registration number to the engine in-process,
  so `SVRegister` never opens a key. The voice catalogue is compiled in and
  served through a `TokenEnums` enumerator, so no per-voice registry entries
  are written either.

The only registry entries created are the two CLSIDs and the one `TokenEnums`
key Windows requires in order to find a speech engine at all. Those must live
under `HKEY_LOCAL_MACHINE` — SAPI reads the enumerator list from there only —
which is why installing needs administrator rights.

### Responsiveness

The host renders a whole utterance in about 14 ms and hands it back in a single
frame — then sits on a fixed **400 ms idle timer** before reporting the
utterance done, refusing to start the next one until it expires. It burns no
CPU doing so; it is a timer, not work.

Waiting for that report is the obvious thing to do, and it costs 400 ms on
every utterance. That lands squarely on interruption: arrow to the next item
and nothing can be said until the previous utterance's timer runs out. Instead,
this engine treats an utterance as finished once its audio has arrived and a
brief quiet period has passed, and sends `CMD_STOP`, which cancels the timer.
Interruption is also polled for while waiting, so a keypress is acted on within
milliseconds rather than whenever the next block of audio happens to turn up.

Measured end to end through a live `ISpVoice` with `sv_latency`, against
Microsoft's own voices on the same machine:

| Voice | From silence | Interrupting speech already playing |
|---|---|---|
| **SoftVoice** | **13–17 ms** | **18 ms** (worst 21 ms) |
| Microsoft David Desktop | 23 ms | 36 ms (worst 41 ms) |
| Microsoft Zira Desktop | 24 ms | 47 ms (worst 63 ms) |

A 1997 synthesiser running in a separate process answers a keypress in about
half the time Microsoft's in-process voices do. That is close to the floor: the
engine itself takes 12–15 ms to render anything at all, near enough
independent of how much text it is given, so roughly four fifths of what
remains is the 1997 binary rather than this wrapper. Dropping to 8000 Hz saves
0.6 ms — there is no quality-for-latency trade worth making.

## Logging

Both interfaces, the utility and the tools write to
`%LOCALAPPDATA%\SoftVoice SAPI5\Logs`, one file per process, at a level chosen
in the utility (off through trace). Errors are always logged whatever the
level. The installer writes a full log and copies it to `install.log` beside
the program. There is a Start menu shortcut to the log folder, and a button for
it in the utility.

## Building

```bash
powershell -File tools\build_all.ps1
```

Builds both architectures, stages `output\`, runs the SAPI self-test against
both DLLs, and compiles the installer into `dist\`.

**Requirements:** Visual Studio 2022 Build Tools (ATL is *not* needed), CMake
3.20+, Windows SDK 10, [Inno Setup 6](https://jrsoftware.org/isinfo.php), and
Python 3 for the check scripts.

Pass `-Version 1.2.0` to stamp a version, `-SkipInstaller` to stop after
staging, or `-Probe` to also build the accessibility probe installer — the same
wizard with no payload and no elevation, for walking the pages with a screen
reader without installing anything.

## Tools

| | |
|---|---|
| `sv_render` | Render a WAV straight from the engine, no SAPI involved. Every parameter is a command-line option. |
| `sv_speak` | Speak or render through SAPI 5, exactly as an application would. `--list` shows every SAPI voice Windows can see. |
| `sv_selftest` | Drive the DLL's class objects directly — no registration, no elevation — and assert the SAPI contract. `--time` benchmarks the engine. |
| `sv_latency` | Time the whole path an application uses: `ISpVoice::Speak` to audible sound, including a true mid-speech interrupt. |
| `tools\check_params.py` | Assert that every exposed parameter changes the audio. |
| `tools\check_clipping.py` | Count wraparound distortion in every voice. |
| `tools\check_settings.py` | Assert that settings persist and reach the engine. |
| `tools\check_config_a11y.py` | Read the settings dialog through MSAA. |
| `tools\check_installer_a11y.py` | Walk the installer wizard through MSAA, page by page. |
| `tools\measure_latency.py` | Time the host protocol: start-up, and time to first audio. |
| `tools\measure_presets.py` | Recover each personality's own rate. |
| `tools\render_samples.py` | Render the whole catalogue to `samples\`. |

One measurement worth knowing before writing a test against this engine: it is
**not sample-exact between two identical renders**. The same preset spoken
twice differs in up to 7% of samples, so a byte comparison proves nothing about
whether a parameter had an effect. `check_params.py` compares against that
run-to-run noise floor instead.

## Credits

- **SoftVoice, Inc.** and **The Productivity Works** for the engine, and for
  pwWebSpeak.
- [**seedy60/svWebspeak**](https://github.com/seedy60/svWebspeak) — the NVDA
  add-on that keeps the engine alive, and the source of the host protocol and
  much of the hard-won knowledge about the engine's parameters that this
  wrapper is built on.

## Licence

The SAPI 5 wrapper, the configuration utility, the tools and the installer in
this repository are open source.

`SVctl32.DLL`, `SVENG32.DLL`, `Svspan32.dll` and `svwebspeak-host.exe` are
proprietary 1997 SoftVoice, Inc. / Productivity Works files and are **not**
covered by it. They are included so the engine can be built and used; no
licence to redistribute them is granted or implied by this repository.
