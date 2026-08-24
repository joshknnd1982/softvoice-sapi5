#!/usr/bin/env python3
"""Talk to svwebspeak-host.exe over its loopback protocol.

The host is the 32-bit process that owns the 1997 SoftVoice engine
(SVctl32.DLL + SVENG32.DLL + Svspan32.dll). It never opens an audio device;
it hands back raw PCM, so this module can render straight to WAV.

Nothing here touches the Windows registry and nothing goes through SAPI 4:
the shipped host and SVctl32.DLL are patched to supply the SoftVoice
registration number in-process (see bin/patch_binaries.py).
"""

import os
import queue
import socket
import struct
import subprocess
import threading
import wave

HOST_EXE = "svwebspeak-host.exe"

CMD_SPEAK, CMD_STOP, CMD_PARAM, CMD_SHUTDOWN = 1, 2, 3, 4
EVT_AUDIO, EVT_DONE = 1, 2

# Engine parameters, as the host numbers them.
P_RATE, P_PITCH, P_VOLUME, P_PERSONALITY, P_INFLECTION = 1, 2, 3, 4, 5
P_LANGUAGE = 6
P_GLOTTAL, P_F0STYLE, P_VOICINGMODE = 7, 8, 9
P_BREATH, P_ROUGHNESS, P_VOWEL, P_GENDER, P_AVBIAS = 10, 11, 12, 13, 14
P_MAKEUP = 15

LANG_EN, LANG_ES = 0x1, 0x2

# Index order is the reverse of the order the names sit in SVctl32.DLL.
PERSONALITIES = (
    "Male", "Female", "Large Male", "Child", "Giant Male", "Mellow Female",
    "Mellow Male", "Crisp Male", "The Fly", "Robotoid", "Martian", "Colossus",
    "Fast Fred", "Old Woman", "Munchkin", "Troll", "Nerd", "Milktoast",
    "Tipsy", "Choir Boy",
)

# Each personality's own pitch in engine units, read from SVGetVoiceInfo.
NATURAL_PITCH = (
    90, 200, 80, 350, 45, 190, 110, 125, 480, 90,
    80, 66, 135, 270, 90, 110, 140, 120, 145, 310,
)


class HostError(RuntimeError):
    pass


class SoftVoiceHost(object):
    """One host process, driven synchronously."""

    def __init__(self, engine_dir, rate=22050, bits=16, timeout=30.0):
        self.engine_dir = os.path.abspath(engine_dir)
        self.rate = int(rate)
        self.bits = int(bits)
        self.timeout = timeout
        self._msg_id = 0
        self._seq = 0
        self._utt = 0
        self._audio = queue.Queue()
        self._init_event = threading.Event()
        self._init = None
        self._lock = threading.Lock()
        self._start()

    # ------------------------------------------------------------- startup

    def _start(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(self.timeout)
        addr, port = server.getsockname()

        cmd = [
            os.path.join(self.engine_dir, HOST_EXE),
            "--address", "%s:%d" % (addr, port),
            "--dir", self.engine_dir,
            "--rate", str(self.rate),
            "--bits", str(self.bits),
        ]
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = subprocess.SW_HIDE
        self.proc = subprocess.Popen(
            cmd, startupinfo=si, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            self.conn, _peer = server.accept()
        except socket.timeout:
            self.proc.kill()
            raise HostError("host did not connect within %.0fs" % self.timeout)
        finally:
            server.close()
        self.conn.settimeout(None)

        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        if not self._init_event.wait(self.timeout):
            raise HostError("engine did not report init status")
        rc, rate, bits, langs = self._init
        if rc != 0:
            raise HostError(
                "engine init failed, status=%d%s" % (
                    rc,
                    " (7025 = engine reports itself unregistered; the shipped "
                    "binaries are patched to register in-process, so this "
                    "means an unpatched SVctl32.DLL or host is in use)"
                    if rc == 7025 else
                    " (-4 = SV_KEY missing from the registry)" if rc == -4
                    else ""))
        self.engine_rate = rate
        self.engine_bits = bits
        self.languages = langs

    # ---------------------------------------------------------------- wire

    def _read_loop(self):
        try:
            while True:
                (length,) = struct.unpack("<I", self._recv_exact(4))
                payload = self._recv_exact(length)
                kind = payload[0]
                if kind == 2:
                    msg_id, _status = struct.unpack_from("<II", payload, 1)
                    if msg_id == 0:
                        self._init = struct.unpack_from("<iIII", payload, 9)
                        self._init_event.set()
                elif kind == 3:
                    (evt,) = struct.unpack_from("<H", payload, 1)
                    if evt == EVT_AUDIO:
                        seq, n = struct.unpack_from("<II", payload, 3)
                        self._audio.put(("audio", seq, payload[11:11 + n]))
                    elif evt == EVT_DONE:
                        seq, utt = struct.unpack_from("<II", payload, 3)
                        self._audio.put(("done", seq, utt))
        except Exception:
            self._audio.put(("closed", 0, b""))
            self._init_event.set()

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.conn.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("host closed")
            buf += chunk
        return buf

    def _send(self, cmd, payload=b""):
        with self._lock:
            self._msg_id += 1
            body = b"\x01" + struct.pack("<IH", self._msg_id, cmd) + payload
            self.conn.sendall(struct.pack("<I", len(body)) + body)

    # ------------------------------------------------------------- control

    def param(self, param, value):
        self._send(CMD_PARAM, struct.pack("<Hi", int(param), int(value)))

    def speak(self, text, timeout=60.0):
        """Render one utterance and return its PCM. Blocks until done."""
        self._utt += 1
        utt = self._utt
        data = text.encode("mbcs", "replace")
        self._send(CMD_SPEAK,
                   struct.pack("<III", self._seq, utt, len(data)) + data)
        chunks = []
        while True:
            kind, seq, payload = self._audio.get(timeout=timeout)
            if kind == "closed":
                raise HostError("host closed while rendering")
            if seq != self._seq:
                continue
            if kind == "audio":
                chunks.append(payload)
            elif kind == "done" and payload == utt:
                return b"".join(chunks)

    def close(self):
        try:
            self._send(CMD_SHUTDOWN)
        except Exception:
            pass
        try:
            self.conn.close()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=5)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def write_wav(path, pcm, rate, bits=16, channels=1):
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(bits // 8)
        w.setframerate(rate)
        w.writeframes(pcm)
