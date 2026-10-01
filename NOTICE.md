# Notices

The code written for this project is licensed under the MIT License (see [LICENSE](LICENSE)). The material
below is not covered by that licence and stays under its own terms.

The code the licence covers is the SAPI 5 wrapper's own code (`src/common/`, and `src/sapi/` apart from the files
listed below), the configuration utility (`src/config/`), the tools (`bin/patch_binaries.py`, and `tools/` apart from
`tools/svhost.py`), the installer (`installer/`) and the build scripts.

## Not covered: the SoftVoice engine

`bin/SVctl32.DLL`, `bin/SVENG32.DLL` and `bin/Svspan32.dll` are the SoftVoice engine: proprietary 1997 files of
SoftVoice, Inc. and The Productivity Works (SoftVoice is a formant synthesiser written by SoftVoice, Inc. and
licensed to The Productivity Works, who shipped it as the speech engine inside pwWebSpeak). They are not covered by
this project's MIT License. `SVctl32.DLL` is patched here by `bin/patch_binaries.py` so that it registers in-process.
They are included so the engine can be built and used; no licence to redistribute them is granted or implied by this
repository.

`bin/svwebspeak-host.exe`, the 32-bit host process that runs the engine, is listed in the README with the files
above, and the same applies to it. It is also the host of the svWebspeak NVDA add-on by seedy60
([seedy60/svWebspeak](https://github.com/seedy60/svWebspeak), which had no licence file in October 2026; its source is
`host/svwebspeak-host.c` there), and the host protocol this wrapper speaks comes from that add-on, as the Credits say.

## Not covered: parts adapted from seedy60's driver

`tools/svhost.py` shares its host-protocol constants, the list of the twenty personalities with their natural
pitches, and its socket and process set-up with the driver of the svWebspeak NVDA add-on
(`addon/synthDrivers/svWebspeak/__init__.py` in [seedy60/svWebspeak](https://github.com/seedy60/svWebspeak)). It is
not covered by this project's MIT License, and no licence is claimed for it here.

## Not covered: SAPI 5 plumbing adapted from gozaltech/BstSpeech-sapi

These files were adapted from the SAPI 5 wrapper [gozaltech/BstSpeech-sapi](https://github.com/gozaltech/BstSpeech-sapi),
which had no licence file in October 2026. They are renamed and reformatted versions of gozaltech's files, with
changes. They are not covered by this project's MIT License, and no licence is claimed for them here.

| Here | gozaltech's file |
|---|---|
| `src/sapi/sv_com.hpp`, `src/sapi/sv_com.cpp` | `src/com.hpp`, `src/com.cpp` |
| `src/sapi/sv_registry.hpp`, `src/sapi/sv_registry.cpp` | `src/registry.hpp`, `src/registry.cpp` |
| `src/sapi/sv_utils.hpp` | `src/utils.hpp` |
| `src/sapi/sv_datakey.hpp`, `src/sapi/sv_datakey.cpp` | `src/ISpDataKeyImpl.hpp`, `src/ISpDataKeyImpl.cpp` |
| `src/sapi/sv_enum_tokens.hpp`, `src/sapi/sv_enum_tokens.cpp` | `src/IEnumSpObjectTokensImpl.hpp`, `src/IEnumSpObjectTokensImpl.cpp` |
| `src/sapi/sv_token.hpp`, `src/sapi/sv_token.cpp` | `src/voice_token.hpp`, `src/voice_token.cpp` |
| `src/sapi/sv_main.cpp` | `src/sapi_main.cpp` |
| `src/sapi/sv_engine.hpp` | `src/ISpTTSEngineImpl.hpp` |
| `src/sapi/sv_engine.cpp` (reworked; it still shares part of the structure of the original) | `src/ISpTTSEngineImpl.cpp` |
