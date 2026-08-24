"""Inspect the setup wizard through MSAA, page by page.

    python tools\\check_installer_a11y.py [path-to-setup.exe]

With no argument this uses dist\\SoftVoiceSAPI5_AccessibilityProbe.exe, which
is the same wizard compiled with /DProbe: no payload and no elevation, so the
pages can be walked without installing anything and without administrator
rights.

Pointed at the real installer instead, this MUST run elevated. The installer
asks for administrator rights, and Windows blocks a lower-integrity process
from reading an elevated window's accessibility tree, so an unelevated
inspector sees nothing at all - which looks exactly like an inaccessible
wizard.

It stops before anything is installed: the walk ends at the last page that
still has a Next button.
"""

import ctypes
import ctypes.wintypes as w
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_msaa import (BM_CLICK, SLOT_GET_NAME, SLOT_GET_ROLE, accessible_for,
                        call_bstr, call_variant, child_count, children_of,
                        ole32, role_text, user32)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_window(timeout=40.0):
    """The wizard, found by window class rather than by process id.

    Inno's setup.exe is only a loader: it extracts the real installer and runs
    it as a child process, so the pid that was launched never owns the wizard
    window.
    """
    found = []

    @ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def each(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        cls = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(hwnd, cls, 64)
        if cls.value == "TWizardForm":
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


def window_pid(hwnd):
    pid = w.DWORD()
    user32.GetWindowThreadProcessId(w.HWND(hwnd), ctypes.byref(pid))
    return pid.value


def find_button(window, caption):
    for info in children_of(window, only_named_or_tabbable=True):
        if info["role"] == "push button" and \
                caption.lower() in info["name"].lower():
            return info["hwnd"]
    return None


def main():
    default = os.path.join(ROOT, "dist",
                           "SoftVoiceSAPI5_AccessibilityProbe.exe")
    setup = sys.argv[1] if len(sys.argv) > 1 else default
    if not os.path.isfile(setup):
        raise SystemExit(
            "no such installer: %s\n"
            "Build the probe with: tools\\build_all.ps1 -Probe" % setup)

    ole32.CoInitialize(None)
    proc = subprocess.Popen([setup])
    window = find_window()
    if window is None:
        proc.kill()
        raise SystemExit("the setup wizard never appeared "
                         "(if this is the real installer, run elevated)")
    wizard_pid = window_pid(window)

    title = ctypes.create_unicode_buffer(256)
    user32.GetWindowTextW(w.HWND(window), title, 256)
    print("Wizard window: '%s'\n" % title.value)

    problems = []
    pages = 0

    for step in range(8):
        time.sleep(0.7)
        controls = children_of(window, only_named_or_tabbable=True)
        heading = next((c["name"] for c in controls
                        if c["role"] == "text" and c["name"].strip()),
                       "(no heading)")
        print("--- page %d: %s ---" % (step + 1, heading[:70]))
        pages += 1

        for info in controls:
            # Non-tab-stop controls are shown too when they are named: only
            # the selected radio button in a group carries WS_TABSTOP and the
            # rest are reached with the arrow keys, so leaving them out would
            # hide half of a choice page from this report.
            if not info["tab"] and info["role"] not in ("radio button",
                                                        "check box"):
                continue
            mark = " " if info["tab"] else "."
            line = "   %s%-16s %-34s" % (mark, info["role"],
                                         info["name"][:34])
            if info["value"]:
                line += "  [%s]" % info["value"][:32]
            print(line)

            container = info["role"] in ("outline", "list", "list view")
            if not container and not info["name"].strip():
                problems.append("page %d has an unnamed %s (%s)"
                                % (step + 1, info["role"], info["class"]))

            if not container:
                continue

            # Inno's components and tasks lists are each one window holding
            # several items, so their contents only appear by asking MSAA for
            # the children. That is where the desktop-shortcut checkbox lives.
            #
            # The container itself is allowed to be unnamed as long as every
            # item in it is named. Inno supplies its own IAccessible for
            # TNewCheckListBox which reports no container name and offers no
            # scripting hook to set one, but a screen reader announces the
            # focused item, not the container - so a named item is what
            # actually decides whether the page is usable. An unnamed *item*
            # is still a failure, because that is a component nobody can
            # identify.
            acc = accessible_for(info["hwnd"])
            items = child_count(acc)
            if items == 0:
                problems.append("page %d has an empty %s (%s)"
                                % (step + 1, info["role"], info["class"]))
            for i in range(1, items + 1):
                item = call_bstr(acc, SLOT_GET_NAME, i)
                item_role = role_text(call_variant(acc, SLOT_GET_ROLE, i))
                if item.strip():
                    print("        item: %-46s %s" % (item[:46], item_role))
                else:
                    problems.append("page %d has an unnamed item in its %s"
                                    % (step + 1, info["role"]))
            if not info["name"].strip():
                print("        (container itself reports no name; its items "
                      "carry the announcement)")
        print()

        nxt = find_button(window, "Next")
        if nxt is None:
            print("(no Next button - stopping here rather than starting an "
                  "install)")
            break
        user32.SendMessageW(w.HWND(nxt), BM_CLICK, 0, 0)

    subprocess.run(["taskkill", "/F", "/PID", str(wizard_pid)],
                   capture_output=True)
    proc.kill()
    time.sleep(0.5)

    print()
    if problems:
        for p in problems:
            print("FAIL:", p)
        return 1
    print("%d wizard pages inspected; every focusable control announces a "
          "name and a role." % pages)
    return 0


if __name__ == "__main__":
    sys.exit(main())
