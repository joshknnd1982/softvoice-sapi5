#!/usr/bin/env python3
"""Measure what each personality's preset actually sets, so the wrapper can
avoid overriding it.

Sending a parameter the user never chose replaces the personality's own value
with a guess. For AV bias that is audible as a crackle - forcing 0 makes 14 of
the 20 voices wrap past full scale. For rate it is audible as every voice
speaking at the same speed, which throws away most of what distinguishes Fast
Fred from Choir Boy.

This measures:

  * whether SVSetVolume(100) is a no-op, i.e. whether 100 is the preset value
  * each personality's own rate, derived from how long a fixed sentence takes
    at the preset versus at a known rate

Both are checked against the engine's own run-to-run variation, because it is
not sample-exact between two identical renders.

    python tools\\measure_presets.py
"""

import array
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svhost
from svhost import P_PERSONALITY, P_RATE, P_VOLUME

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")

TEXT = "The quick brown fox jumps over the lazy dog."


def rms_diff(a, b):
    la, lb = array.array("h"), array.array("h")
    la.frombytes(a)
    lb.frombytes(b)
    n = min(len(la), len(lb))
    if n == 0:
        return 0.0
    return math.sqrt(sum(float(la[i] - lb[i]) ** 2 for i in range(n)) / n)


def main():
    with svhost.SoftVoiceHost(BIN) as h:
        rate = h.engine_rate

        print("Is SVSetVolume(100) a no-op?\n")
        print("  %-16s %10s %10s  %s"
              % ("voice", "noise", "vol=100", "verdict"))
        volume_neutral = True
        for index in (0, 1, 3, 11, 19):
            name = svhost.PERSONALITIES[index]
            h.param(P_PERSONALITY, index)
            base = h.speak(TEXT)
            h.param(P_PERSONALITY, index)
            repeat = h.speak(TEXT)
            h.param(P_PERSONALITY, index)
            h.param(P_VOLUME, 100)
            forced = h.speak(TEXT)
            noise = rms_diff(base, repeat)
            with_vol = rms_diff(base, forced)
            ok = with_vol <= max(noise * 2.0, 1.0)
            volume_neutral = volume_neutral and ok
            print("  %-16s %10.2f %10.2f  %s"
                  % (name, noise, with_vol, "no-op" if ok else "CHANGES THE AUDIO"))
        print("\n  -> SVSetVolume(100) is %s\n"
              % ("safe to send always" if volume_neutral
                 else "NOT neutral; only send it when the user chose a volume"))

        # Duration is inversely proportional to rate, so two known rates
        # establish the constant and the preset's duration then gives its rate.
        print("Each personality's own rate:\n")
        h.param(P_PERSONALITY, 0)
        h.param(P_RATE, 100)
        d100 = len(h.speak(TEXT)) / 2.0 / rate
        h.param(P_PERSONALITY, 0)
        h.param(P_RATE, 200)
        d200 = len(h.speak(TEXT)) / 2.0 / rate
        print("  proportionality check on Male: rate 100 -> %.3f s, "
              "rate 200 -> %.3f s" % (d100, d200))
        print("  rate x duration = %.1f and %.1f (equal means duration is "
              "1/rate)\n" % (100 * d100, 200 * d200))

        print("  %-16s %9s %9s %9s" % ("voice", "preset s", "@150 s", "rate"))
        table = []
        for index, name in enumerate(svhost.PERSONALITIES):
            h.param(P_PERSONALITY, index)
            preset = len(h.speak(TEXT)) / 2.0 / rate
            h.param(P_PERSONALITY, index)
            h.param(P_RATE, 150)
            at150 = len(h.speak(TEXT)) / 2.0 / rate
            natural = int(round(150.0 * at150 / preset))
            table.append((name, natural))
            print("  %-16s %9.3f %9.3f %9d" % (name, preset, at150, natural))

        print("\nNATURAL_RATE table:\n")
        for i in range(0, len(table), 5):
            row = table[i:i + 5]
            print("    " + ", ".join("%3d" % r for _n, r in row) + ",")
        print("\n  (order: %s)" % ", ".join(n for n, _r in table))
    return 0


if __name__ == "__main__":
    sys.exit(main())
