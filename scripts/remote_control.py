#!/usr/bin/env python3
"""Manually drive the Game & Watch UI over the debug probe — keyboard or gamepad.

Thin front-end over the shared backend in `scripts/common/remote_input.py`. The
device mechanism lives there; this file is input capture + a key map, exposed as
pollable InputSource objects so the fastcap capture tool can drive the device
live while it records.

REQUIRES a firmware built with the remote-input hook compiled in (default on;
opt out with `make REMOTE_INPUT=0`). A build without the hook does nothing.

Modes
-----
  auto (default)  try a gamepad first (Linux evdev / Pygame); fall back to the keyboard if
                  none is found.
  --gamepad       force a controller via evdev/pygame. OS key-up events give TRUE
                  press/release — let go and it releases instantly.
  --keyboard      force raw-terminal keypresses. Terminals send key-down but no
                  key-up, so a held key stays down for --hold ms and releases when
                  the terminal's auto-repeat stops (release lags by ~--hold ms).

`WindowKeySource` here is the same idea for the fastcap OpenCV live window
(`fastcap.py capture --live`): watch the stream and drive in one window.

Keyboard map
------------
    arrows = D-pad      a/Enter = A      b/Backspace = B
    s = Start           Tab = Select     p = Pause
    g = Game            t = Time         P (shift) = Power
    q / Ctrl-C = quit (clears the shadow cell on the way out)

The Left+Game combo is the launcher-escape macro (works in the menu and inside a
running stock game). Hold Left, tap Game — or just press both.
"""
from __future__ import annotations

import argparse
import atexit
import os
import select
import sys
import time
from pathlib import Path

# Platform flags
IS_WINDOWS = sys.platform == "win32"

if not IS_WINDOWS:
    import termios
    import tty

# Add the scripts directory to sys.path so we can import remote_input.py locally
sys.path.insert(0, str(Path(__file__).resolve().parent))
import remote_input as ri

# Single-byte / control keys -> button bit.
KEYMAP = {
    "\r": ri.BTN_A, "\n": ri.BTN_A, "a": ri.BTN_A,
    "\x7f": ri.BTN_B, "\b": ri.BTN_B, "b": ri.BTN_B,
    "s": ri.BTN_START, "\t": ri.BTN_SELECT,
    "p": ri.BTN_PAUSE, "g": ri.BTN_GAME, "t": ri.BTN_TIME,
    "P": ri.BTN_PWR,
}
ARROWS = {
    "\x1b[A": ri.BTN_UP, "\x1b[B": ri.BTN_DOWN,
    "\x1b[C": ri.BTN_RIGHT, "\x1b[D": ri.BTN_LEFT,
}


def _parse_keys(buf: str):
    """Yield button bits from a raw stdin chunk; None signals quit."""
    i = 0
    while i < len(buf):
        if buf[i] == "\x1b":
            seq = buf[i:i + 3]
            if seq in ARROWS:
                yield ARROWS[seq]
                i += 3
                continue
            yield None  # lone ESC = quit
            return
        ch = buf[i]
        if ch in ("q", "\x03"):
            yield None
            return
        if ch in KEYMAP:
            yield KEYMAP[ch]
        i += 1


# ---- Pollable input sources ----------------------------------------------

class InputSource:
    def open(self) -> "InputSource":
        return self

    def close(self) -> None:
        pass

    def poll(self, now: float):  # -> int mask | None
        raise NotImplementedError


class KeyboardSource(InputSource):
    """Raw-terminal keypresses with a hold timer (cross-platform compatible)."""

    def __init__(self, hold_ms: int = 150):
        self.hold_s = hold_ms / 1000.0
        self._expiry: dict[int, float] = {}
        self._fd = None
        self._old = None

    def open(self) -> "KeyboardSource":
        if not IS_WINDOWS:
            self._fd = sys.stdin.fileno()
            self._old = termios.tcgetattr(self._fd)
            atexit.register(self._restore)
            tty.setraw(self._fd)
        return self

    def _restore(self) -> None:
        if not IS_WINDOWS and self._old is not None and self._fd is not None:
            try:
                termios.tcsetattr(self._fd, termios.TCSADRAIN, self._old)
            except Exception:
                pass

    def close(self) -> None:
        self._restore()

    def poll(self, now: float):
        if IS_WINDOWS:
            import msvcrt
            while msvcrt.kbhit():
                ch = msvcrt.getch()
                # Intercept Windows extended scan codes (Arrows)
                if ch in (b"\x00", b"\xe0"):
                    ch2 = msvcrt.getch()
                    win_arrows = {b"H": ri.BTN_UP, b"P": ri.BTN_DOWN, b"M": ri.BTN_RIGHT, b"K": ri.BTN_LEFT}
                    if ch2 in win_arrows:
                        self._expiry[win_arrows[ch2]] = now + self.hold_s
                else:
                    try:
                        decoded = ch.decode("utf-8", errors="ignore")
                        if decoded in ("q", "\x03"):
                            return None
                        if decoded in KEYMAP:
                            self._expiry[KEYMAP[decoded]] = now + self.hold_s
                    except Exception:
                        pass
        else:
            r, _, _ = select.select([self._fd], [], [], 0)
            if r:
                try:
                    raw_bytes = os.read(self._fd, 64)
                    buf = raw_bytes.decode("utf-8", errors="ignore")

                    for bit in _parse_keys(buf):
                        if bit is None:
                            return None
                        self._expiry[bit] = now + self.hold_s
                except OSError:
                    pass

        self._expiry = {b: t for b, t in self._expiry.items() if t > now}
        mask = 0
        for b in self._expiry:
            mask |= 1 << b
        return mask


# ---- Gamepad Backends -----------------------------------------------------

class EvdevGamepadBackend:
    """Linux-native evdev backend mapping raw kernel event character devices."""
    def __init__(self, path: str | None = None):
        self.path = path
        self._pad = None
        self._held = 0
        self._keymap = None
        self._ecodes = None

    def open(self):
        from evdev import InputDevice, ecodes, list_devices
        self._ecodes = ecodes

        if self.path:
            self._pad = InputDevice(self.path)
        else:
            for dp in list_devices():
                d = InputDevice(dp)
                caps = d.capabilities().get(ecodes.EV_KEY, [])
                if ecodes.BTN_SOUTH in caps or ecodes.BTN_GAMEPAD in caps:
                    self._pad = d
                    break
            if not self._pad:
                raise RuntimeError("No evdev gamepad found")

        self._keymap = {
            ecodes.BTN_SOUTH: ri.BTN_A, ecodes.BTN_EAST: ri.BTN_B,
            ecodes.BTN_START: ri.BTN_START, ecodes.BTN_SELECT: ri.BTN_SELECT,
            ecodes.BTN_TR: ri.BTN_GAME, ecodes.BTN_TL: ri.BTN_TIME,
            ecodes.BTN_THUMBL: ri.BTN_PAUSE, ecodes.BTN_MODE: ri.BTN_PWR,
        }
        return self

    @property
    def name(self) -> str:
        return f"{self._pad.name} ({self._pad.path})"

    def close(self) -> None:
        if self._pad is not None:
            try:
                self._pad.close()
            except Exception:
                pass

    def _apply(self, ev) -> None:
        ec = self._ecodes
        if ev.type == ec.EV_KEY and ev.code in self._keymap:
            bit = 1 << self._keymap[ev.code]
            if ev.value:
                self._held |= bit
            else:
                self._held &= ~bit
        elif ev.type == ec.EV_ABS:
            if ev.code == ec.ABS_HAT0X:
                self._held &= ~((1 << ri.BTN_LEFT) | (1 << ri.BTN_RIGHT))
                if ev.value < 0:
                    self._held |= 1 << ri.BTN_LEFT
                elif ev.value > 0:
                    self._held |= 1 << ri.BTN_RIGHT
            elif ev.code == ec.ABS_HAT0Y:
                self._held &= ~((1 << ri.BTN_UP) | (1 << ri.BTN_DOWN))
                if ev.value < 0:
                    self._held |= 1 << ri.BTN_UP
                elif ev.value > 0:
                    self._held |= 1 << ri.BTN_DOWN

    def poll(self, now: float) -> int:
        while True:
            try:
                ev = self._pad.read_one()
            except (BlockingIOError, OSError):
                break
            if ev is None:
                break
            self._apply(ev)
        return self._held


class PygameGamepadBackend:
    """Cross-platform SDL backend for macOS, Windows, and local Linux hosts."""
    def __init__(self, path: str | None = None):
        self.index = int(path) if (path and path.isdigit()) else 0
        self._joystick = None
        self._held = 0

    def open(self):
        # Prevent Pygame from initializing an unnecessary GUI window context
        if "SDL_VIDEODRIVER" not in os.environ:
            os.environ["SDL_VIDEODRIVER"] = "dummy"

        import pygame
        pygame.init()
        pygame.joystick.init()

        if pygame.joystick.get_count() == 0:
            raise RuntimeError("No gamepads found via SDL backend")

        self._joystick = pygame.joystick.Joystick(self.index)
        self._joystick.init()
        return self

    @property
    def name(self) -> str:
        return f"{self._joystick.get_name()} (SDL Index {self.index})"

    def close(self) -> None:
        import pygame
        if self._joystick:
            self._joystick.quit()
        pygame.joystick.quit()

    def poll(self, now: float) -> int:
        import pygame
        pygame.event.pump()

        self._held = 0
        num_buttons = self._joystick.get_numbuttons()

        # Dynamic mapping layout capturing standard SDL mapping profiles (Xbox, DualShock, Switch)
        button_map = {
            0: ri.BTN_A,       # South
            1: ri.BTN_B,       # East
            4: ri.BTN_SELECT,  # Back / Share / Select
            6: ri.BTN_SELECT,
            7: ri.BTN_START,   # Start Options
            11: ri.BTN_START,
            9: ri.BTN_TIME,    # Left Bumper / Shoulder
            10: ri.BTN_GAME,   # Right Bumper / Shoulder
            8: ri.BTN_PAUSE,   # Guide / Home system fallbacks
            12: ri.BTN_PWR,
        }

        for btn_idx, btn_bit in button_map.items():
            if btn_idx < num_buttons and self._joystick.get_button(btn_idx):
                self._held |= (1 << btn_bit)

        # Evaluate the directional Hat (D-Pad)
        if self._joystick.get_numhats() > 0:
            hat_x, hat_y = self._joystick.get_hat(0)
            if hat_x < 0:  self._held |= (1 << ri.BTN_LEFT)
            elif hat_x > 0: self._held |= (1 << ri.BTN_RIGHT)
            if hat_y > 0:  self._held |= (1 << ri.BTN_UP)    # SDL Up axis maps positively
            elif hat_y < 0: self._held |= (1 << ri.BTN_DOWN)

        return self._held


class GamepadSource(InputSource):
    """Proxy abstraction layer routing to evdev on Linux or pygame on macOS/Windows."""

    def __init__(self, path: str | None = None):
        self.path = path
        self._backend = None

    def open(self) -> "GamepadSource":
        if sys.platform == "linux":
            try:
                self._backend = EvdevGamepadBackend(self.path).open()
                return self
            except Exception:
                pass  # Fall back to pygame if evdev fails inside the platform environment

        self._backend = PygameGamepadBackend(self.path).open()
        return self

    @property
    def name(self) -> str:
        return self._backend.name if self._backend else "?"

    def close(self) -> None:
        if self._backend:
            self._backend.close()

    def poll(self, now: float):
        return self._backend.poll(now) if self._backend else 0


class WindowKeySource(InputSource):
    """Keyboard control fed from an OpenCV window's waitKeyEx()."""

    DPAD = {
        ord("w"): ri.BTN_UP,   ord("a"): ri.BTN_LEFT,
        ord("s"): ri.BTN_DOWN, ord("d"): ri.BTN_RIGHT,
        65362: ri.BTN_UP, 65364: ri.BTN_DOWN,
        65361: ri.BTN_LEFT, 65363: ri.BTN_RIGHT,
    }
    BTNS = {
        ord("j"): ri.BTN_A,     13: ri.BTN_A,
        ord("k"): ri.BTN_B,      8: ri.BTN_B,
        ord("u"): ri.BTN_START, ord("i"): ri.BTN_SELECT,
        ord("g"): ri.BTN_GAME,  ord("t"): ri.BTN_TIME,
        ord("p"): ri.BTN_PAUSE, ord("o"): ri.BTN_PWR,
    }
    QUIT = {ord("q"), 27}
    KEY_HELP = ("WASD/arrows=D-pad  J/Enter=A  K/Bksp=B  U=Start  I=Select  "
                "G=Game  T=Time  P=Pause  O=Power  |  F=FPS  Q/Esc=quit")

    def __init__(self, hold_ms: int = 150):
        self.hold_s = hold_ms / 1000.0
        self._expiry: dict[int, float] = {}
        self.quit = False

    def feed(self, key: int, now: float) -> None:
        if key < 0:
            return
        if key in self.QUIT:
            self.quit = True
        elif key in self.DPAD:
            self._expiry[self.DPAD[key]] = now + self.hold_s
        elif key in self.BTNS:
            self._expiry[self.BTNS[key]] = now + self.hold_s

    def poll(self, now: float):
        self._expiry = {b: t for b, t in self._expiry.items() if t > now}
        mask = 0
        for b in self._expiry:
            mask |= 1 << b
        return mask


def open_input_source(prefer: str = "auto", device: str | None = None, hold_ms: int = 150):
    if prefer == "none":
        return None
    if prefer in ("auto", "gamepad"):
        try:
            src = GamepadSource(device).open()
            print(f"  Input: gamepad {src.name}")
            return src
        except Exception as e:
            if prefer == "gamepad":
                raise
            print(f"  Input: no gamepad ({e}); using keyboard")
    src = KeyboardSource(hold_ms).open()
    print("  Input: keyboard (raw terminal)")
    return src


def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = p.add_mutually_exclusive_group()
    g.add_argument("--gamepad", action="store_true", help="force a controller via evdev/pygame")
    g.add_argument("--keyboard", action="store_true", help="force raw-terminal keyboard")
    p.add_argument("--device", help="explicit evdev path (/dev/input/eventN) or pygame index (0, 1...)")
    p.add_argument("--hold", type=int, default=150,
                   help="keyboard: ms a key stays held before auto-release (default 150)")
    args = p.parse_args()

    prefer = "gamepad" if args.gamepad else "keyboard" if args.keyboard else "auto"
    with ri.session() as dev:
        src = open_input_source(prefer, args.device, args.hold)
        print("--- driving (q / Ctrl-C to quit) ---")
        last = -1
        try:
            while True:
                now = time.monotonic()
                mask = src.poll(now)
                if mask is None:
                    break
                if mask != last:
                    dev.transport.write_mask(mask)
                    last = mask
                time.sleep(0.005)
        except KeyboardInterrupt:
            pass
        finally:
            if src:
                src.close()
    print("\nCleared shadow cell. Bye.")


if __name__ == "__main__":
    main()
