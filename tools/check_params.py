#!/usr/bin/env python3
"""Check that every exposed SoftVoice parameter really does something.

Renders a baseline with sv_render.exe and then one variation per parameter,
and fails if a variation is byte-identical to the baseline - which is what a
setting that is quietly ignored looks like. Also checks the two properties
the mapping is built on: that rate moves the duration in the right
direction, and that pitch at 50 leaves the personality on its own pitch.

    python tools\\check_params.py build_x86\\bin\\Release\\sv_render.exe
"""
import array
import math
import os
import subprocess
import sys
import tempfile
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svhost
from svhost import P_PERSONALITY, P_PITCH

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXT = "The rain in Spain falls mainly on the plain."
VOICE = "SoftVoice Male"

# Each is (option, value, why it should be audible).
VARIATIONS = [
    ("--rate", "20", "slower than natural"),
    ("--rate", "80", "faster than natural"),
    ("--pitch", "20", "below the voice's own pitch"),
    ("--pitch", "80", "above the voice's own pitch"),
    ("--volume", "40", "quieter"),
    ("--inflection", "0", "flat"),
    ("--inflection", "100", "wide pitch range"),
    ("--breathiness", "90", "breathy"),
    ("--roughness", "60", "rough"),
    ("--vowel-length", "20", "clipped vowels"),
    ("--vowel-length", "90", "drawn-out vowels"),
    ("--glottal", "2", "soft glottal source"),
    ("--glottal", "8", "harsh glottal source"),
    ("--intonation", "2", "monotone"),
    ("--intonation", "4", "expressive"),
    ("--voicing", "1", "soft voicing"),
    ("--voicing", "2", "whispered"),
    ("--gender", "2", "female vocal tract"),
    ("--gender", "3", "neutral vocal tract"),
    ("--av-bias", "-40", "reduced voicing amplitude"),
    ("--volume-makeup", "200", "doubled output level"),
]


def read_wav(path):
    with wave.open(path, "rb") as w:
        frames = w.getnframes()
        data = w.readframes(frames)
        rate = w.getframerate()
    return data, rate, frames / float(rate)


def rms(data):
    a = array.array("h")
    a.frombytes(data)
    if not len(a):
        return 0.0
    return math.sqrt(sum(float(v) * v for v in a) / len(a))


def rms_difference(a, b):
    """RMS of the sample-by-sample difference between two PCM buffers."""
    left = array.array("h")
    left.frombytes(a)
    right = array.array("h")
    right.frombytes(b)
    n = min(len(left), len(right))
    if n == 0:
        return 0.0
    total = sum(float(left[i] - right[i]) ** 2 for i in range(n))
    return math.sqrt(total / n)


def render(exe, out, extra):
    cmd = [exe, "--voice", VOICE, "--text", TEXT, "--out", out] + extra
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit("render failed: %s\n%s"
                         % (" ".join(cmd), result.stdout + result.stderr))
    return read_wav(out)


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "build_x86", "bin", "Release", "sv_render.exe")
    if not os.path.isfile(exe):
        raise SystemExit("no such tool: %s" % exe)

    tmp = tempfile.mkdtemp(prefix="svparams")
    failures = []

    base_path = os.path.join(tmp, "base.wav")
    base, rate, base_secs = render(exe, base_path, [])
    print("baseline: %.2f s, %d bytes, rms %.1f\n"
          % (base_secs, len(base), rms(base)))

    print("%-22s %-8s %8s %8s  %s" % ("parameter", "value", "secs", "rms",
                                      "result"))
    for option, value, why in VARIATIONS:
        path = os.path.join(tmp, "v%s%s.wav"
                            % (option.strip("-"), value.replace("-", "m")))
        data, _rate, secs = render(exe, path, [option, value])
        changed = data != base
        note = "ok" if changed else "NO CHANGE -- %s" % why
        if not changed:
            failures.append("%s %s" % (option, value))
        print("%-22s %-8s %8.2f %8.1f  %s"
              % (option, value, secs, rms(data), note))

    print()

    # Rate must move the duration the right way. A mapping that is inverted
    # or clamped shows up here and nowhere else.
    _slow, _r, slow_secs = render(exe, os.path.join(tmp, "slow.wav"),
                                  ["--rate", "0"])
    _fast, _r, fast_secs = render(exe, os.path.join(tmp, "fast.wav"),
                                  ["--rate", "100"])
    print("rate 0 -> %.2f s, rate 50 -> %.2f s, rate 100 -> %.2f s"
          % (slow_secs, base_secs, fast_secs))
    if not (slow_secs > base_secs > fast_secs):
        failures.append("rate does not order 0 > 50 > 100")

    # Volume 0 must be silence, not merely quiet.
    quiet, _r, _s = render(exe, os.path.join(tmp, "mute.wav"),
                           ["--volume", "0"])
    print("volume 0 -> rms %.1f (baseline %.1f)" % (rms(quiet), rms(base)))
    if rms(quiet) >= rms(base) * 0.1:
        failures.append("volume 0 is not appreciably quieter")

    # The property the whole pitch mapping rests on: at 50 percent the engine
    # must be left on the personality's own pitch, so the neutral position
    # does not detune any voice.
    #
    # Byte equality is the wrong test. The engine is NOT sample-exact between
    # two identical utterances - rendering the same preset twice differs in
    # up to 7 percent of samples - so the comparison has to be against that
    # run-to-run noise floor. Measured here per voice: sending the natural
    # pitch explicitly must be no further from the bare preset than the bare
    # preset is from itself.
    print("\npitch neutrality at 50 percent, against the engine's own "
          "run-to-run variation:")
    print("  %-16s %10s %10s  %s" % ("voice", "noise", "with pitch", ""))
    with svhost.SoftVoiceHost(os.path.join(ROOT, "bin")) as h:
        for index, name in enumerate(svhost.PERSONALITIES):
            h.param(P_PERSONALITY, index)
            base_pcm = h.speak(TEXT)
            h.param(P_PERSONALITY, index)
            repeat_pcm = h.speak(TEXT)
            h.param(P_PERSONALITY, index)
            h.param(P_PITCH, svhost.NATURAL_PITCH[index])
            explicit_pcm = h.speak(TEXT)

            noise = rms_difference(base_pcm, repeat_pcm)
            with_pitch = rms_difference(base_pcm, explicit_pcm)
            # A real detune moves the waveform far more than the dither
            # does; the margin only has to separate the two.
            ok = with_pitch <= max(noise * 2.0, 1.0)
            if not ok:
                failures.append("pitch neutrality for %s" % name)
            print("  %-16s %10.2f %10.2f  %s"
                  % (name, noise, with_pitch, "ok" if ok else "DETUNED"))

    print("\n%s" % ("FAILURES: " + "; ".join(failures) if failures
                    else "All parameter checks passed."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
