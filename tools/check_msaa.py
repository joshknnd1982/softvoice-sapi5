"""Read a Win32 window the way NVDA does, through MSAA.

Shared by check_config_a11y.py and check_installer_a11y.py.

MSAA (oleacc), not UI Automation, is the interface that decides whether a
standard Win32 dialog announces itself: PowerShell's managed UIAutomation
client reports every standard control as an unnamed Pane with the control's
value in its Name, which looks exactly like a broken dialog even when the
dialog is perfect. NVDA uses MSAA for these, so MSAA is both the correct and
the authoritative check.

Two ctypes traps are handled here so callers do not have to know about them:
IID_IAccessible must be laid out in GUID byte order, and a COM method
prototype declared with WINFUNCTYPE(...)(slot, "m") must NOT list `this` in
its argument types - ctypes supplies the interface pointer itself.
"""

import ctypes
import ctypes.wintypes as w

user32 = ctypes.WinDLL("user32", use_last_error=True)
oleacc = ctypes.WinDLL("oleacc")
ole32 = ctypes.WinDLL("ole32")
oleaut32 = ctypes.WinDLL("oleaut32")

OBJID_CLIENT = 0xFFFFFFFC
CHILDID_SELF = 0

GWL_STYLE = -16
WS_TABSTOP = 0x00010000
WS_DISABLED = 0x08000000
WS_VISIBLE = 0x10000000

WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
BM_CLICK = 0x00F5

# IAccessible vtable slots, after IUnknown (3) and IDispatch (4).
SLOT_GET_CHILD_COUNT = 8
SLOT_GET_NAME = 10
SLOT_GET_VALUE = 11
SLOT_GET_ROLE = 13
SLOT_GET_STATE = 14


class VARIANT(ctypes.Structure):
    _fields_ = [("vt", ctypes.c_ushort), ("r1", ctypes.c_ushort),
                ("r2", ctypes.c_ushort), ("r3", ctypes.c_ushort),
                ("lVal", ctypes.c_longlong), ("pad", ctypes.c_longlong)]


def _variant(child_id):
    v = VARIANT()
    v.vt = 3            # VT_I4
    v.lVal = child_id
    return v


def call_bstr(acc, slot, child_id=CHILDID_SELF):
    """One of the BSTR-returning IAccessible properties."""
    if not acc:
        return ""
    proto = ctypes.WINFUNCTYPE(ctypes.c_long, VARIANT,
                               ctypes.POINTER(ctypes.c_void_p))
    fn = proto(slot, "m")
    out = ctypes.c_void_p()
    if fn(acc, _variant(child_id), ctypes.byref(out)) != 0 or not out:
        return ""
    text = ctypes.wstring_at(out)
    oleaut32.SysFreeString(out)
    return text


def call_variant(acc, slot, child_id=CHILDID_SELF):
    """One of the VARIANT-returning IAccessible properties, as an integer."""
    if not acc:
        return None
    proto = ctypes.WINFUNCTYPE(ctypes.c_long, VARIANT, ctypes.POINTER(VARIANT))
    fn = proto(slot, "m")
    out = VARIANT()
    if fn(acc, _variant(child_id), ctypes.byref(out)) != 0:
        return None
    return out.lVal


def child_count(acc):
    if not acc:
        return 0
    proto = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.POINTER(ctypes.c_long))
    fn = proto(SLOT_GET_CHILD_COUNT, "m")
    out = ctypes.c_long()
    if fn(acc, ctypes.byref(out)) != 0:
        return 0
    return out.value


def role_text(role):
    if role is None:
        return "?"
    buf = ctypes.create_unicode_buffer(128)
    n = oleacc.GetRoleTextW(ctypes.c_ulong(role), buf, 128)
    return buf.value if n else "role %d" % role


def accessible_for(hwnd):
    # {618736E0-3C3D-11CF-810C-00AA00389B71}, in GUID byte order.
    iid = (ctypes.c_byte * 16)(
        *bytes.fromhex("E03687613D3CCF11810C00AA00389B71"))
    acc = ctypes.c_void_p()
    hr = oleacc.AccessibleObjectFromWindow(
        w.HWND(hwnd), ctypes.c_ulong(OBJID_CLIENT), ctypes.byref(iid),
        ctypes.byref(acc))
    return acc if hr == 0 and acc else None


def window_text(hwnd):
    """The control's own caption, including its access-key ampersand.

    Via WM_GETTEXT rather than GetWindowText: the latter silently returns
    nothing for a control owned by another process, which reads as an
    unlabelled dialog when the dialog is fine.
    """
    length = user32.SendMessageW(w.HWND(hwnd), WM_GETTEXTLENGTH, 0, 0)
    if length <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(length + 1)
    user32.SendMessageW(w.HWND(hwnd), WM_GETTEXT, length + 1,
                        ctypes.byref(buf))
    return buf.value


def describe(hwnd):
    """Everything a screen reader would use about one control."""
    cls = ctypes.create_unicode_buffer(64)
    user32.GetClassNameW(w.HWND(hwnd), cls, 64)
    style = user32.GetWindowLongW(w.HWND(hwnd), GWL_STYLE)
    acc = accessible_for(hwnd)
    return {
        "hwnd": hwnd,
        "id": user32.GetDlgCtrlID(w.HWND(hwnd)),
        "class": cls.value,
        "tab": bool(style & WS_TABSTOP),
        "disabled": bool(style & WS_DISABLED),
        "visible": bool(style & WS_VISIBLE),
        "name": call_bstr(acc, SLOT_GET_NAME),
        "role": role_text(call_variant(acc, SLOT_GET_ROLE)),
        "value": call_bstr(acc, SLOT_GET_VALUE),
        "caption": window_text(hwnd),
    }


def children_of(window, only_named_or_tabbable=False):
    items = []

    @ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    def each(hwnd, _):
        info = describe(hwnd)
        if not only_named_or_tabbable or (
                info["visible"] and (info["name"].strip() or info["tab"])):
            items.append(info)
        return True

    user32.EnumChildWindows(w.HWND(window), each, 0)
    return items


def tab_order(dialog):
    """The real tab order, asked of Windows rather than simulated.

    GetNextDlgTabItem is the function the dialog manager itself calls when Tab
    is pressed, so this is the order a user gets, obtained without stealing
    focus from whatever the tester is doing.
    """
    order = []
    first = user32.GetNextDlgTabItem(w.HWND(dialog), None, False)
    current = first
    while current:
        order.append(describe(current))
        current = user32.GetNextDlgTabItem(w.HWND(dialog), w.HWND(current),
                                           False)
        if current == first:
            break
    return order
