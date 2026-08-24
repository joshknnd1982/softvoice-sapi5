#!/usr/bin/env python3
"""Render a WAV for every SoftVoice personality in every language.

    python tools\\render_samples.py [output_dir]

Produces one file per voice per language plus a combined tour of each
language, so the whole catalogue can be listened through without SAPI.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svhost
from svhost import (
    LANG_EN, LANG_ES, PERSONALITIES, P_LANGUAGE, P_PERSONALITY,
)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")

EN_TEXT = ("This is the SoftVoice %s voice. "
           "The quick brown fox jumps over the lazy dog.")
ES_TEXT = ("Esta es la voz %s de SoftVoice. "
           "El veloz murcielago hindu comia feliz cardillo y kiwi.")

LANGS = (
    ("en", LANG_EN, "English", EN_TEXT),
    ("es", LANG_ES, "Spanish", ES_TEXT),
)


def slug(name):
    return re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_")


def main(argv):
    out = os.path.abspath(argv[1]) if len(argv) > 1 \
        else os.path.join(ROOT, "samples")
    os.makedirs(out, exist_ok=True)

    with svhost.SoftVoiceHost(BIN) as h:
        print("engine: %d Hz, %d bit, languages=0x%x"
              % (h.engine_rate, h.engine_bits, h.languages))
        # Half a second of silence between voices in the combined tour.
        gap = b"\0" * (h.engine_rate * 2 // 2)
        total = 0

        for code, bit, lang_name, template in LANGS:
            if not h.languages & bit:
                print("%s not loaded by the engine; skipping" % lang_name)
                continue
            tour = []
            for i, voice in enumerate(PERSONALITIES):
                # The personality preset resets prosody, so it is selected
                # first and the voice is then heard exactly as it ships.
                h.param(P_PERSONALITY, i)
                h.param(P_LANGUAGE, bit)
                pcm = h.speak(template % voice)
                name = "%s_%02d_%s.wav" % (code, i + 1, slug(voice))
                svhost.write_wav(os.path.join(out, name), pcm, h.engine_rate)
                secs = len(pcm) / 2.0 / h.engine_rate
                print("  %-28s %5.2fs  %s" % (name, secs, voice))
                tour.append(pcm)
                tour.append(gap)
                total += 1
            combined = "00_all_voices_%s.wav" % code
            svhost.write_wav(os.path.join(out, combined),
                             b"".join(tour), h.engine_rate)
            print("  %-28s %5.2fs  (all %d voices, %s)"
                  % (combined,
                     sum(len(c) for c in tour) / 2.0 / h.engine_rate,
                     len(PERSONALITIES), lang_name))
            total += 1

    print("\n%d files written to %s" % (total, out))


if __name__ == "__main__":
    main(sys.argv)
