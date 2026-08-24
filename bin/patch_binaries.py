#!/usr/bin/env python3
"""Make the SoftVoice engine and host register in-process, with no registry.

This documents and reproduces the two binary patches that turn the licence-
gated svWebspeak binaries into the fully self-contained ones shipped in this
add-on. Run it against *pristine* copies of the 1997 SoftVoice host and engine
(the ones the original svWebspeak / a pwWebSpeak install carried):

    python patch_binaries.py svwebspeak-host.exe SVctl32.DLL

Each file is patched in place only after its original bytes are verified, so it
is safe to run once; running it again is refused because the original bytes no
longer match.

Background (see doc/en/readme.html for the short version):

* The 1997 SVctl32.DLL clips every utterance to ~0.4s unless the engine is
  registered. Registration is gated on a registry key,
  HKLM\SOFTWARE\SoftVoice\ProdWorks, that SVctl32's own SVRegister reads.
* SV_KEY = 0x3CBC5AA8 is a valid registration number; it only needs that
  registry gate satisfied.

The patches remove the two registry reads and feed the number in-process:

1. svwebspeak-host.exe @ VA 0x401331 (file offset 0x731)
   `mov [esp+8], eax` (89 44 24 08 ...) becomes
   `mov dword [esp+8], 0x3CBC5AA8` + `jmp 0x40139E`
   -> the host no longer reads SV_KEY from the registry; it supplies the
      number directly to the engine's SVRegister.

2. SVctl32.DLL @ VA 0x1C011097 (file offset 0x10497)
   `mov edi, 0x1C017244` (BF 44 72 01 1C ...) becomes
   `mov ebp, [esp+0x74]` + `mov esi, [esp+0x78]` + `jmp 0x1C011153`
   -> SVRegister skips its RegOpenKeyExA/RegQueryValueExA and goes straight to
      the crypto that validates the number, which then unlocks full audio.

Requires: pip install pefile
"""
import sys
import pefile

SV_KEY = 0x3CBC5AA8


def _patch(path, image_base, va, expect_prefix, new_bytes, label):
    pe = pefile.PE(path, fast_load=True)
    assert pe.OPTIONAL_HEADER.ImageBase == image_base, (
        "%s: unexpected image base 0x%x (expected 0x%x)"
        % (path, pe.OPTIONAL_HEADER.ImageBase, image_base))
    foff = pe.get_offset_from_rva(va - image_base)
    pe.close()  # release the memory map before we rewrite the file in place
    data = bytearray(open(path, "rb").read())
    orig = bytes(data[foff:foff + len(new_bytes)])
    if orig == new_bytes:
        print("%s: already patched, skipping." % label)
        return
    if orig[:len(expect_prefix)] != expect_prefix:
        raise SystemExit(
            "%s: original bytes at 0x%x are %s, not the expected %s. "
            "This is not a pristine binary; refusing to patch."
            % (label, va, orig.hex(" "), expect_prefix.hex(" ")))
    data[foff:foff + len(new_bytes)] = new_bytes
    open(path, "wb").write(data)
    print("%s: patched %d bytes at VA 0x%x (file 0x%x)."
          % (label, len(new_bytes), va, foff))


def patch_host(path):
    va = 0x401331
    mov = bytes([0xC7, 0x44, 0x24, 0x08]) + SV_KEY.to_bytes(4, "little")
    rel = 0x40139E - (va + len(mov) + 5)
    new = mov + bytes([0xE9]) + (rel & 0xFFFFFFFF).to_bytes(4, "little")
    _patch(path, 0x400000, va, bytes([0x89, 0x44, 0x24, 0x08]), new,
           "svwebspeak-host.exe")


def patch_engine(path):
    va = 0x1C011097
    body = bytes([0x8B, 0x6C, 0x24, 0x74,   # mov ebp,[esp+0x74]  (arg2)
                  0x8B, 0x74, 0x24, 0x78])  # mov esi,[esp+0x78]  (arg3)
    rel = 0x1C011153 - (va + len(body) + 5)
    new = body + bytes([0xE9]) + (rel & 0xFFFFFFFF).to_bytes(4, "little")
    _patch(path, 0x1C000000, va, bytes([0xBF, 0x44, 0x72, 0x01, 0x1C]), new,
           "SVctl32.DLL")


def main(argv):
    if len(argv) != 3:
        raise SystemExit(
            "usage: patch_binaries.py <svwebspeak-host.exe> <SVctl32.DLL>")
    patch_host(argv[1])
    patch_engine(argv[2])
    print("Done. Both binaries now register the engine in-process; no "
          "registry entries are read or written.")


if __name__ == "__main__":
    main(sys.argv)
