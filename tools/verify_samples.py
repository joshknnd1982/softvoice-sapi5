"""Sanity-check the rendered samples and prove the language switch works."""
import array
import glob
import math
import os
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svhost
from svhost import LANG_EN, LANG_ES, P_LANGUAGE, P_PERSONALITY

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def stats(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes()
        a = array.array("h")
        a.frombytes(w.readframes(n))
        rate = w.getframerate()
    if not len(a):
        return 0.0, 0, 0.0
    peak = max(abs(v) for v in a)
    rms = math.sqrt(sum(float(v) * v for v in a) / len(a))
    return n / float(rate), peak, rms


bad = []
files = sorted(glob.glob(os.path.join(ROOT, "samples", "*.wav")))
print("%-28s %8s %7s %8s" % ("file", "secs", "peak", "rms"))
for f in files:
    secs, peak, rms = stats(f)
    flag = ""
    if secs < 1.0 or peak < 1000:
        flag = "  <-- SUSPECT"
        bad.append(os.path.basename(f))
    print("%-28s %8.2f %7d %8.1f%s"
          % (os.path.basename(f), secs, peak, rms, flag))

print()
print("Language switch check: identical text, en vs es")
with svhost.SoftVoiceHost(os.path.join(ROOT, "bin")) as h:
    h.param(P_PERSONALITY, 0)
    text = "Gracias, senorita. Vamos a la playa."
    h.param(P_LANGUAGE, LANG_EN)
    en = h.speak(text)
    h.param(P_LANGUAGE, LANG_ES)
    es = h.speak(text)
    print("  en: %d bytes (%.2fs)" % (len(en), len(en) / 2.0 / h.engine_rate))
    print("  es: %d bytes (%.2fs)" % (len(es), len(es) / 2.0 / h.engine_rate))
    if en == es:
        print("  IDENTICAL -- the language switch had no effect")
        bad.append("language switch")
    else:
        common = min(len(en), len(es))
        diff = sum(1 for i in range(0, common, 64) if en[i] != es[i])
        print("  DIFFERENT: %d/%d sampled bytes differ, %d byte length delta"
              % (diff, common // 64, abs(len(en) - len(es))))
    svhost.write_wav(os.path.join(ROOT, "samples", "_langtest_en.wav"),
                     en, h.engine_rate)
    svhost.write_wav(os.path.join(ROOT, "samples", "_langtest_es.wav"),
                     es, h.engine_rate)

print()
print("FAILURES: %s" % (", ".join(bad) if bad else "none"))
