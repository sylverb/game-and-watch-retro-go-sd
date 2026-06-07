"""Remote button injection backend — the shared engine for driving the device.

How it works
------------
A `REMOTE_INPUT` firmware build OR's a 32-bit "shadow" word at
`SRAM_REMOTE_INPUT_ADDR` (0x2001FFF4, DTCM) into its live button state every
input poll, so a debug-probe write to that cell is indistinguishable from a
physical press.

The whole point of this module is **one persistent OpenOCD connection** to keep
overhead low and avoid repeating commands.
"""
from __future__ import annotations

import time
from contextlib import contextmanager

# --- Shadow cell: keep in sync with Core/Inc/gw_buttons.h ---
SHADOW_ADDR = 0x2001FFF4

# --- Button bits: keep in sync with input_button_t enum order:
#     UP, DOWN, LEFT, RIGHT, A, B, START, SELECT, PAUSE, GAME, TIME, PWR
BTN_UP = 0
BTN_DOWN = 1
BTN_LEFT = 2
BTN_RIGHT = 3
BTN_A = 4
BTN_B = 5
BTN_START = 6
BTN_SELECT = 7
BTN_PAUSE = 8
BTN_GAME = 9
BTN_TIME = 10
BTN_PWR = 11

# Name <-> bit, handy for CLIs / logging.
NAMES = {
    "UP": BTN_UP, "DOWN": BTN_DOWN, "LEFT": BTN_LEFT, "RIGHT": BTN_RIGHT,
    "A": BTN_A, "B": BTN_B, "START": BTN_START, "SELECT": BTN_SELECT,
    "PAUSE": BTN_PAUSE, "GAME": BTN_GAME, "TIME": BTN_TIME, "PWR": BTN_PWR,
}
BIT_NAME = {v: k for k, v in NAMES.items()}

# Default tap timing.
TAP_MS = 80
GAP_MS = 120


def mask_of(keys) -> int:
    """OR a button or iterable of buttons into a single bitmask."""
    if isinstance(keys, int):
        keys = (keys,)
    m = 0
    for k in keys:
        m |= 1 << int(k)
    return m


def mask_str(mask: int) -> str:
    """Human-readable button list for a mask (e.g. 'LEFT+GAME')."""
    parts = [BIT_NAME[b] for b in range(16) if mask & (1 << b)]
    return "+".join(parts) if parts else "none"


class InputTransport:
    """Abstract way to assert a raw button bitmask on the device."""

    def open(self) -> "InputTransport":
        return self

    def close(self) -> None:
        pass

    def reconnect(self) -> "InputTransport":
        """Re-establish the link after a device reset."""
        try:
            self.close()
        except Exception:
            pass
        return self.open()

    def write_mask(self, mask: int) -> None:
        raise NotImplementedError


class ShadowCellTransport(InputTransport):
    """Writes the button bitmask to the SWD shadow cell over one OpenOCD socket."""

    def __init__(self, backend=None, addr: int = SHADOW_ADDR):
        self._backend = backend
        self._own_backend = backend is None
        self._addr = addr

    def open(self) -> "ShadowCellTransport":
        if self._backend is None:
            from gnwmanager.ocdbackend.openocd_backend import OpenOCDBackend
            self._backend = OpenOCDBackend()
            self._backend.open()
        return self

    def close(self) -> None:
        if self._own_backend and self._backend is not None:
            self._backend.close()
            self._backend = None

    def reconnect(self) -> "ShadowCellTransport":
        if not self._own_backend:
            raise RuntimeError("cannot reconnect a shared (externally-owned) backend")
        self._backend = None
        return self.open()

    def write_mask(self, mask: int) -> None:
        self._backend.write_uint32(self._addr, mask & 0xFFFFFFFF)

    @property
    def backend(self):
        """The live OpenOCD backend (for memory reads)."""
        return self._backend


class _Hold:
    """Context manager that holds a mask down for the duration of a `with` block."""

    def __init__(self, dev: "RemoteInput", mask: int):
        self._dev = dev
        self._mask = mask

    def __enter__(self) -> "_Hold":
        self._dev._add_held(self._mask)
        return self

    def __exit__(self, *exc) -> bool:
        self._dev._remove_held(self._mask)
        return False


class _NullCM:
    """Inert context manager so `with button_press([X])` is harmless when hold=False."""

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class RemoteInput:
    """Press buttons on the device over a persistent connection."""

    def __init__(self, transport: InputTransport | None = None):
        self.transport = transport or ShadowCellTransport()
        self._held = 0
        self._open = False

    def open(self) -> "RemoteInput":
        self.transport.open()
        self._open = True
        self._held = 0
        self.transport.write_mask(0)
        return self

    def close(self) -> None:
        if self._open:
            try:
                self.transport.write_mask(0)
            finally:
                self.transport.close()
                self._open = False

    def __enter__(self) -> "RemoteInput":
        return self.open()

    def __exit__(self, *exc) -> bool:
        self.close()
        return False

    @property
    def backend(self):
        return getattr(self.transport, "backend", None)

    def reconnect(self) -> "RemoteInput":
        self._held = 0
        self.transport.reconnect()
        self._open = True
        try:
            self.transport.write_mask(0)
        except Exception:
            pass
        return self

    def _flush(self, transient: int = 0) -> None:
        self.transport.write_mask(self._held | transient)

    def _add_held(self, mask: int) -> None:
        self._held |= mask
        self._flush()

    def _remove_held(self, mask: int) -> None:
        self._held &= ~mask
        self._flush()

    def tap(self, keys, repeat: int = 1, tap_ms: int = TAP_MS, gap_ms: int = GAP_MS) -> None:
        """Press and release `keys` cleanly `repeat` times."""
        mask = mask_of(keys)
        for i in range(repeat):
            self._flush(mask)
            time.sleep(tap_ms / 1000.0)
            self._flush(0)
            if i + 1 < repeat:
                time.sleep(gap_ms / 1000.0)

    def hold(self, keys) -> _Hold:
        """Return a context manager that holds `keys` for the `with` block."""
        return _Hold(self, mask_of(keys))

    def button_press(self, keys, hold: bool = False, repeat: int = 1):
        if hold:
            return self.hold(keys)
        self.tap(keys, repeat=repeat)
        return _NullCM()

    def press(self, keys, **kw):
        return self.button_press(keys, **kw)


# --- module-level convenience over a single default instance -------------
_default: RemoteInput | None = None


def connect(transport: InputTransport | None = None) -> RemoteInput:
    global _default
    if _default is None:
        _default = RemoteInput(transport)
        _default.open()
    return _default


def close() -> None:
    global _default
    if _default is not None:
        _default.close()
        _default = None


def _require() -> RemoteInput:
    if _default is None:
        raise RuntimeError("call remote_input.connect() first")
    return _default


def button_press(keys, hold: bool = False, repeat: int = 1):
    return _require().button_press(keys, hold=hold, repeat=repeat)


def tap(keys, repeat: int = 1):
    _require().tap(keys, repeat=repeat)


@contextmanager
def session(transport: InputTransport | None = None):
    dev = RemoteInput(transport)
    dev.open()
    try:
        yield dev
    finally:
        dev.close()
