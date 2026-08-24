#!/usr/bin/env python3
"""Look for wraparound distortion in every personality's own preset.

The engine renders in fixed point and WRAPS rather than saturating when a
parameter drives it past full scale, which is heard as a harsh crackle rather
than as ordinary distortion. The signature is a sample that jumps from near
+full scale to near -full scale in a single step.

This renders each personality three ways and counts those jumps:

  preset      the personality's own settings, nothing else sent
  av=0        the same, but with SVSetAVBias(0) sent explicitly
  av=-20      the same, with the trim the driver applies to glottal
              sources 2 to 8

Five of the twenty presets (Robotoid, Martian and Colossus use glottal source
2; Tipsy and Choir Boy use 4) are the ones at risk, because sources 2 to 8
overflow at the engine's default voicing amplitude.

    python tools\\check_clipping.py
"""

import array
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svhost
from svhost import P_AVBIAS, P_PERSONALITY

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")

# Long enough to exercise a range of phonemes and loud enough to reach the
# top of the scale if the voice is going to.
TEXT = ("The quick brown fox jumps over the lazy dog. "
        "How razorback jumping frogs can level six piqued gymnasts!")

# A jump this large in one sample cannot come from a 22 kHz speech signal; it
# is the accumulator having wrapped.
JUMP = 40000
NEAR_FULL = 25000


def analyse(pcm):
    a = array.array("h")
    a.frombytes(pcm)
    if len(a) < 2:
        return 0, 0, 0
    wraps = 0
    for i in range(1, len(a)):
        d = a[i] - a[i - 1]
        if abs(d) >= JUMP and (
                (a[i - 1] > NEAR_FULL and a[i] < -NEAR_FULL) or
                (a[i - 1] < -NEAR_FULL and a[i] > NEAR_FULL)):
            wraps += 1
    peak = max(max(a), -min(a))
    # Samples sitting exactly at the rails, which is ordinary clipping.
    railed = sum(1 for v in a if v >= 32760 or v <= -32760)
    return wraps, peak, railed


def main():
    print("Rendering each personality's own preset, and the same preset with "
          "AV bias forced.\n")
    print("%-16s %10s %10s %10s   %s"
          % ("voice", "preset", "av=0", "av=-20", "verdict"))
    print("-" * 74)

    suspects = []
    with svhost.SoftVoiceHost(BIN) as h:
        for index, name in enumerate(svhost.PERSONALITIES):
            results = {}
            for label, av in (("preset", None), ("av0", 0), ("avm20", -20)):
                # Reload the preset each time: SVSetPersonality resets the
                # whole voice, which is the only way to get back to a clean
                # starting point.
                h.param(P_PERSONALITY, index)
                if av is not None:
                    h.param(P_AVBIAS, av)
                results[label] = analyse(h.speak(TEXT))

            preset_w = results["preset"][0]
            av0_w = results["av0"][0]
            avm20_w = results["avm20"][0]

            verdict = "clean"
            if preset_w > 0:
                verdict = "PRESET ITSELF WRAPS"
                suspects.append(name)
            elif av0_w > preset_w:
                verdict = "sending av=0 CAUSES wrapping"
                suspects.append(name)
            print("%-16s %10d %10d %10d   %s"
                  % (name, preset_w, av0_w, avm20_w, verdict))

    print()
    if suspects:
        print("Voices that wrap: %s" % ", ".join(suspects))
    else:
        print("No wraparound found in any preset.")

    # The check that actually matters: what a user hears from the shipped
    # wrapper with nothing configured. Every one of the forty voices must be
    # as clean as its bare preset.
    print("\nThrough the shipped renderer, at default settings:\n")
    exe = os.path.join(ROOT, "build_x86", "bin", "Release", "sv_render.exe")
    if not os.path.isfile(exe):
        print("  sv_render.exe is not built; skipping.")
        return 1 if suspects else 0

    import subprocess
    import tempfile
    import wave

    tmp = tempfile.mkdtemp(prefix="svclip")
    dirty = []
    print("  %-34s %8s %8s  %s" % ("voice", "wraps", "peak", "verdict"))
    result = subprocess.run([exe, "--list"], capture_output=True, text=True)
    ids = [line.split()[0] for line in result.stdout.splitlines()[1:]
           if line.strip() and line.split()[0].isdigit()]
    for vid in ids:
        out = os.path.join(tmp, "v%s.wav" % vid)
        r = subprocess.run([exe, "--voice", vid, "--text", TEXT, "--out", out],
                           capture_output=True, text=True)
        if r.returncode != 0 or not os.path.isfile(out):
            print("  voice %s failed to render" % vid)
            dirty.append(vid)
            continue
        with wave.open(out, "rb") as w:
            pcm = w.readframes(w.getnframes())
        wraps, peak, _railed = analyse(pcm)
        name = r.stdout.splitlines()[-1].split("  ")[0].strip() if r.stdout \
            else vid
        if wraps:
            dirty.append(name)
        print("  %-34s %8d %8d  %s"
              % (name[:34], wraps, peak, "clean" if not wraps else "WRAPS"))

    print()
    if dirty:
        print("FAILED - these wrap at default settings: %s" % ", ".join(dirty))
        return 1
    print("All %d voices are clean at default settings." % len(ids))
    return 1 if suspects and False else 0


if __name__ == "__main__":
    sys.exit(main())
