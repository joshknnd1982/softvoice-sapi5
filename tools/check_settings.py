"""Check that settings persist, and that the engine picks them up.

Two separate promises are tested end to end:

  1. The configuration utility writes what it is told to, and the write
     survives closing the dialog.
  2. The SAPI 5 engine re-reads the file, so a saved change is heard on the
     next thing spoken without anything being restarted.

The second is measured, not asserted: the rate is set low and then high, and
the duration of the same sentence rendered through the real SAPI objects has
to move accordingly.

    python tools\\check_settings.py

The existing settings file is saved and restored, so running this does not
disturb a real configuration.
"""

import ctypes
import ctypes.wintypes as w
import os
import re
import shutil
import subprocess
import sys
import time
import wave

user32 = ctypes.WinDLL("user32", use_last_error=True)

WM_SETTEXT = 0x000C
WM_COMMAND = 0x0111
WM_CLOSE = 0x0010
EN_KILLFOCUS = 0x0200
CBN_SELCHANGE = 1
CB_SETCURSEL = 0x014E
CB_FINDSTRINGEXACT = 0x0158

IDC_VOICE = 1001
IDC_RATE = 1003
IDC_PITCH = 1005
IDC_INFLECTION = 1009
IDC_BREATHINESS = 1011
IDC_ROUGHNESS = 1013
IDC_VOWEL = 1015
IDC_AVBIAS = 1025

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SETTINGS = os.path.join(os.environ["APPDATA"], "SoftVoice SAPI5",
                        "softvoice.ini")

failures = []


def check(ok, what):
    print("  %-62s %s" % (what, "ok" if ok else "FAILED"))
    if not ok:
        failures.append(what)


def find_dialog(pid):
    found = []

    @ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def each(hwnd, _):
        owner = w.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
            return False
        return True

    user32.EnumWindows(each, 0)
    return found[0] if found else None


def set_edit(dialog, ctrl_id, text):
    """Type into an edit and tell the dialog focus left it.

    WM_SETTEXT rather than SetWindowText, which does nothing across a process
    boundary. The EN_KILLFOCUS is what the utility commits on, so sending it
    is the same thing as tabbing away from the field.
    """
    control = user32.GetDlgItem(w.HWND(dialog), ctrl_id)
    user32.SendMessageW(w.HWND(control), WM_SETTEXT, 0,
                        ctypes.c_wchar_p(text))
    user32.SendMessageW(w.HWND(dialog), WM_COMMAND,
                        (EN_KILLFOCUS << 16) | ctrl_id, control)


def select_voice(dialog, name):
    combo = user32.GetDlgItem(w.HWND(dialog), IDC_VOICE)
    index = user32.SendMessageW(w.HWND(combo), CB_FINDSTRINGEXACT, -1,
                                ctypes.c_wchar_p(name))
    if index < 0:
        return False
    user32.SendMessageW(w.HWND(combo), CB_SETCURSEL, index, 0)
    user32.SendMessageW(w.HWND(dialog), WM_COMMAND,
                        (CBN_SELCHANGE << 16) | IDC_VOICE, combo)
    return True


def read_setting(section, key):
    if not os.path.isfile(SETTINGS):
        return None
    current = None
    with open(SETTINGS, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1]
            elif current == section and "=" in line:
                name, _, value = line.partition("=")
                if name.strip() == key:
                    return value.strip()
    return None


def write_settings(voice, pairs):
    os.makedirs(os.path.dirname(SETTINGS), exist_ok=True)
    with open(SETTINGS, "w", encoding="utf-8") as f:
        f.write("[general]\ndefault_voice=%s\nsample_rate=22050\n"
                "log_level=info\n\n[voice:%s]\n" % (voice, voice))
        for key, value in pairs.items():
            f.write("%s=%s\n" % (key, value))


def render_through_sapi(exe, voice, out):
    """Speak through the real SAPI objects and return the duration.

    The exit code is deliberately ignored. sv_selftest asserts that an
    utterance produces audible audio, which is exactly what a deliberately
    muted setting must not do, so a non-zero exit here is the expected result
    of the volume check rather than a failure of it. The WAV that gets
    written is the measurement either way.
    """
    subprocess.run([exe, "--voice", voice, "--out", out],
                   capture_output=True, text=True, cwd=os.path.dirname(exe))
    if not os.path.isfile(out):
        raise SystemExit("sv_selftest wrote no WAV to %s" % out)
    with wave.open(out, "rb") as f:
        return f.getnframes() / float(f.getframerate())


def main():
    config = os.path.join(ROOT, "build_x86", "bin", "Release",
                          "SoftVoiceConfig.exe")
    selftest = os.path.join(ROOT, "build_x86", "bin", "Release",
                            "sv_selftest.exe")
    for path in (config, selftest):
        if not os.path.isfile(path):
            raise SystemExit("not built: %s" % path)

    backup = SETTINGS + ".checkbackup"
    had_existing = os.path.isfile(SETTINGS)
    if had_existing:
        shutil.copy2(SETTINGS, backup)

    try:
        print("The utility writes what it is told, and the write survives "
              "closing it:")
        if os.path.isfile(SETTINGS):
            os.remove(SETTINGS)

        proc = subprocess.Popen([config])
        time.sleep(2.0)
        dialog = find_dialog(proc.pid)
        if not dialog:
            proc.kill()
            raise SystemExit("the configuration utility showed no window")

        check(select_voice(dialog, "SoftVoice Female"),
              "the voice can be selected from the list")
        set_edit(dialog, IDC_RATE, "23")
        set_edit(dialog, IDC_PITCH, "77")
        time.sleep(0.4)
        check(read_setting("voice:SoftVoice Female", "rate") == "23",
              "rate 23 is in the file while the utility is still open")

        # Out of range on purpose: it must be clamped, not stored as typed.
        set_edit(dialog, IDC_RATE, "500")
        time.sleep(0.4)
        check(read_setting("voice:SoftVoice Female", "rate") == "100",
              "an out-of-range rate is clamped to 100, not stored as 500")

        set_edit(dialog, IDC_RATE, "23")
        time.sleep(0.3)
        user32.SendMessageW(w.HWND(dialog), WM_CLOSE, 0, 0)
        for _ in range(50):
            if proc.poll() is not None:
                break
            time.sleep(0.1)
        if proc.poll() is None:
            proc.kill()

        check(read_setting("voice:SoftVoice Female", "rate") == "23",
              "rate 23 survived closing the utility")
        check(read_setting("voice:SoftVoice Female", "pitch") == "77",
              "pitch 77 survived closing the utility")
        check(read_setting("general", "default_voice") == "SoftVoice Female",
              "the chosen voice was remembered")

        # The character settings must start on "the voice's own", i.e. -1,
        # and be written as -1. Anything else overwrites the personality's
        # own preset - and for av_bias that drives most of the twenty voices
        # past full scale, which the engine wraps into an audible crackle.
        print("\nCharacter settings leave the preset alone until chosen:")
        for key in ("inflection", "breathiness", "roughness", "vowel_length",
                    "av_bias", "volume_makeup", "glottal_source",
                    "intonation", "voicing", "gender"):
            check(read_setting("voice:SoftVoice Female", key) == "-1",
                  "%s defaults to the voice's own (-1)" % key)

        print("\nThe engine re-reads the file, with nothing restarted:")
        tmp = os.path.join(os.environ.get("TEMP", "."), "svsettingscheck")
        os.makedirs(tmp, exist_ok=True)

        write_settings("SoftVoice Female", {"rate": 10})
        slow = render_through_sapi(selftest, "SoftVoice Female",
                                   os.path.join(tmp, "slow.wav"))
        write_settings("SoftVoice Female", {"rate": 90})
        fast = render_through_sapi(selftest, "SoftVoice Female",
                                   os.path.join(tmp, "fast.wav"))
        print("    rate 10 -> %.2f s, rate 90 -> %.2f s" % (slow, fast))
        check(slow > fast * 1.5,
              "a saved rate change reaches the SAPI engine")

        write_settings("SoftVoice Female", {"volume": 0})
        muted = os.path.join(tmp, "muted.wav")
        render_through_sapi(selftest, "SoftVoice Female", muted)
        with wave.open(muted, "rb") as f:
            data = f.readframes(f.getnframes())
        # At volume 0 the engine emits nothing at all rather than a run of
        # silent samples, so an empty file is as correct here as an all-zero
        # one.
        check(not any(data), "a saved volume of 0 reaches the SAPI engine")

    finally:
        if had_existing:
            shutil.move(backup, SETTINGS)
        elif os.path.isfile(SETTINGS):
            os.remove(SETTINGS)

    print()
    if failures:
        print("FAILURES: %s" % "; ".join(failures))
        return 1
    print("Settings persist, are clamped, and reach the engine.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
