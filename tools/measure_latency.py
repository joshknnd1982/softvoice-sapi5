#!/usr/bin/env python3
"""Measure where the delay between asking for speech and hearing it goes.

Times the raw host protocol, so the numbers are the engine's and the socket's
alone, with no SAPI in the way:

  * how long starting a host costs, which is paid once
  * how long a parameter frame takes to be accepted
  * how long from CMD_SPEAK to the FIRST audio byte, for texts of the length
    a screen reader actually speaks

The last is the one that matters. The host renders a whole utterance before
sending any of it, so time-to-first-audio is the render time, and that is the
floor on how responsive the voice can be.

    python tools\\measure_latency.py
"""

import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svhost
from svhost import P_PERSONALITY, P_RATE

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")

# What a screen reader actually says while someone arrows around a desktop,
# shortest first. The long one is there for contrast, not because it is
# typical.
SAMPLES = [
    "OK",
    "Desktop",
    "Recycle Bin",
    "Documents folder",
    "Start button, collapsed",
    "Firefox, 3 of 12, list",
    "This is a full sentence of the kind a book reader would send.",
    "This is a much longer passage, the sort of thing a document reader "
    "hands over in one go, and it should show clearly how the time to the "
    "first audible sample grows with the amount of text being rendered.",
]


def time_first_audio(h, text, repeats=5):
    """Milliseconds from sending CMD_SPEAK to the first audio byte."""
    firsts = []
    totals = []
    lengths = []
    for _ in range(repeats):
        h._utt += 1
        utt = h._utt
        data = text.encode("mbcs", "replace")

        start = time.perf_counter()
        h._send(1, struct.pack("<III", h._seq, utt, len(data)) + data)
        first = None
        got = 0
        while True:
            kind, seq, payload = h._audio.get(timeout=60)
            if kind == "audio":
                if first is None:
                    first = time.perf_counter()
                got += len(payload)
            elif kind == "done" and payload == utt:
                break
        end = time.perf_counter()
        firsts.append((first - start) * 1000.0 if first else 0.0)
        totals.append((end - start) * 1000.0)
        lengths.append(got)
    n = len(firsts)
    return (sum(firsts) / n, min(firsts), sum(totals) / n,
            lengths[0] / 2.0 / h.engine_rate)


def main():
    print("Host start-up (paid once per voice object):")
    for attempt in range(3):
        start = time.perf_counter()
        h = svhost.SoftVoiceHost(BIN)
        ready = (time.perf_counter() - start) * 1000.0
        print("  attempt %d: %6.1f ms to a ready engine" % (attempt + 1, ready))
        if attempt < 2:
            h.close()

    print("\nOne parameter frame, round trip not awaited (fire and forget):")
    start = time.perf_counter()
    for _ in range(100):
        h.param(P_RATE, 150)
    print("  %6.3f ms each, averaged over 100" %
          ((time.perf_counter() - start) * 1000.0 / 100))

    print("\nTime from CMD_SPEAK to the FIRST audio byte:")
    print("  %-62s %8s %8s %8s %8s"
          % ("text", "avg ms", "best ms", "total ms", "audio s"))
    h.param(P_PERSONALITY, 0)
    for text in SAMPLES:
        avg, best, total, secs = time_first_audio(h, text)
        shown = text if len(text) <= 60 else text[:57] + "..."
        print("  %-62s %8.1f %8.1f %8.1f %8.2f"
              % (shown, avg, best, total, secs))

    print("\nSame, but a fresh host each time (what a per-utterance engine "
          "object would cost):")
    h.close()
    for text in SAMPLES[:3]:
        start = time.perf_counter()
        fresh = svhost.SoftVoiceHost(BIN)
        fresh.param(P_PERSONALITY, 0)
        avg, best, total, secs = time_first_audio(fresh, text, repeats=1)
        wall = (time.perf_counter() - start) * 1000.0
        fresh.close()
        print("  %-62s %8.1f ms all in" % (text[:60], wall))

    return 0


if __name__ == "__main__":
    sys.exit(main())
