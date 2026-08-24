"""Inspect the SoftVoice configuration dialog the way NVDA sees it.

For each child window this reports the class, whether it is a tab stop, and
the MSAA name, role and value; then it walks the real tab order and checks
the access keys.

    python tools\\check_config_a11y.py [path-to-SoftVoiceConfig.exe]

Exits non-zero if a focusable control has no name, if a control is disabled
(a disabled control drops out of the tab order entirely, so a setting that
does not apply has to say so in its label instead), or if two controls claim
the same Alt+letter - which the resource compiler does not warn about.
"""

import ctypes
import ctypes.wintypes as w
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_msaa import (children_of, ole32, tab_order, user32)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_dialog(pid, timeout=10.0):
    found = []

    @ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def each(hwnd, _):
        owner = w.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
            return False
        return True

    deadline = time.time() + timeout
    while time.time() < deadline:
        found.clear()
        user32.EnumWindows(each, 0)
        if found:
            return found[0]
        time.sleep(0.3)
    return None


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "build_x86", "bin", "Release", "SoftVoiceConfig.exe")
    if not os.path.isfile(exe):
        raise SystemExit("no such utility: %s" % exe)

    proc = subprocess.Popen([exe])
    ole32.CoInitialize(None)
    dialog = find_dialog(proc.pid)
    if not dialog:
        proc.kill()
        raise SystemExit("the configuration utility showed no window")

    title = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(w.HWND(dialog), title, 256)
    print("Dialog: '%s'\n" % title.value)
    print("%-6s %-14s %-9s %-18s %-34s %s"
          % ("id", "class", "tab", "MSAA role", "MSAA name", "MSAA value"))
    print("-" * 118)

    controls = children_of(dialog)
    for c in controls:
        tab = "DISABLED" if c["disabled"] else ("yes" if c["tab"] else "-")
        print("%-6d %-14s %-9s %-18s %-34s %s"
              % (c["id"], c["class"], tab, c["role"], c["name"][:34],
                 c["value"][:28]))

    print("\nTab order, as it would be announced:\n")
    order = tab_order(dialog)
    for n, c in enumerate(order, 1):
        announced = "%s  %s" % (c["name"].strip(), c["role"])
        if c["value"]:
            announced += "  %s" % c["value"]
        print("%2d. %s" % (n, announced))

    # Access keys. Duplicates never warn at compile time and are only found by
    # pressing Alt+letter and landing somewhere unexpected.
    print("\nAccess keys:")
    keys = {}
    duplicates = []
    for c in controls:
        for match in re.finditer(r"&(.)", c["caption"] or ""):
            letter = match.group(1).upper()
            if letter == "&":
                continue
            if letter in keys:
                duplicates.append((letter, keys[letter], c["caption"]))
            else:
                keys[letter] = c["caption"]
    for letter in sorted(keys):
        print("  Alt+%s  %s" % (letter, keys[letter]))

    proc.kill()

    problems = []
    tab_stops = sum(1 for c in controls if c["tab"] and not c["disabled"])
    if len(order) != tab_stops:
        problems.append("the tab order visits %d controls but %d are tab stops"
                        % (len(order), tab_stops))
    for c in order:
        if not c["name"].strip():
            problems.append("tab stop %d announces no name" % c["id"])
    for c in controls:
        if c["disabled"]:
            problems.append("control %d is disabled, so Tab skips it"
                            % c["id"])
    for letter, first, second in duplicates:
        problems.append("Alt+%s is claimed by both '%s' and '%s'"
                        % (letter, first, second))

    print()
    if problems:
        for p in problems:
            print("FAIL:", p)
        return 1
    print("%d tab stops, every one named, none disabled, the tab order "
          "visits them all,\nand all %d access keys are unique."
          % (len(order), len(keys)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
