"""Test-harness and debug-probe helpers for retro-go.

Exposes symbol resolution, state peeking, and target loop checks.
"""
from __future__ import annotations

import signal
import subprocess
import time
from contextlib import contextmanager
from pathlib import Path

# Sane default pointer for resolving symbols
DEFAULT_ELF = Path(__file__).resolve().parents[1] / "build" / "gw_retro_go.elf"


@contextmanager
def time_budget(seconds: float, label: str):
    """Hard wall-clock bound using SIGALRM to prevent hanging on a wedged probe."""
    def _fire(signum, frame):
        raise TimeoutError(f"{label}: exceeded {seconds:.0f}s budget (probe wedged?)")
    old = signal.signal(signal.SIGALRM, _fire)
    signal.setitimer(signal.ITIMER_REAL, seconds)
    try:
        yield
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, old)


def resolve_symbol(name: str, elf: Path | str = DEFAULT_ELF) -> int:
    """Return the load address of a symbol from the ELF symbol table via arm-none-eabi-nm.
    
    Supports LTO name-mangling by matching symbol names starting with the prefix.
    """
    elf_path = Path(elf)
    if not elf_path.exists():
        raise FileNotFoundError(f"ELF file not found at {elf_path}")
    
    out = subprocess.run(
        ["arm-none-eabi-nm", str(elf_path)],
        capture_output=True, text=True, check=True,
    ).stdout
    
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        addr, _kind, sym = parts
        if sym == name or sym.startswith(name + "."):
            return int(addr, 16)
    raise KeyError(f"symbol {name!r} not found in {elf_path}")


def read_u32_symbol(backend, name: str, offset: int = 0, elf: Path | str = DEFAULT_ELF) -> int:
    """Read a uint32 at the resolved symbol (+offset)."""
    return backend.read_uint32(resolve_symbol(name, elf) + offset)


def retrogo_running(backend, settle: float = 0.25, min_ticks: int = 10,
                    max_ticks: int = 100000, budget: float = 15.0, elf: Path | str = DEFAULT_ELF) -> tuple[bool, str]:
    """Return (ok, detail): is the retro-go firmware main loop running?
    
    Validates execution by asserting that the uwTick counter is incrementing.
    """
    try:
        with time_budget(budget, "retrogo_running"):
            addr = resolve_symbol("uwTick", elf)
            backend.halt()
            t0 = backend.read_uint32(addr)
            backend.resume()
            time.sleep(settle)
            backend.halt()
            t1 = backend.read_uint32(addr)
            backend.resume()
    except Exception as e:
        return False, str(e)
    
    delta = (t1 - t0) & 0xFFFFFFFF
    ok = min_ticks <= delta <= max_ticks
    return ok, f"uwTick {t0}->{t1} (+{delta} in {settle:.2f}s)"


def safe_read_u32(backend, addr: int) -> int | None:
    """Read a uint32, returning None instead of raising if the read fails."""
    try:
        return backend.read_uint32(addr)
    except Exception:
        return None


def wait_u32(dev, addr: int, test_fn, timeout: float = 90.0,
             interval: float = 2.0, reconnect: bool = True) -> int | None:
    """Poll an address until test_fn(value) is true or timeout elapses."""
    deadline = time.time() + timeout
    while True:
        if reconnect:
            try:
                dev.reconnect()
            except Exception:
                pass
        last = safe_read_u32(dev.backend, addr)
        if last is not None:
            try:
                if test_fn(last):
                    return last
            except Exception:
                pass
        if time.time() >= deadline:
            return last
        time.sleep(interval)
