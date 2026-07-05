#!/usr/bin/env python3
"""Statistical PC sampler via OpenOCD tcl-rpc (port 6666).

Repeatedly: halt -> read pc -> resume -> random short sleep.
The SWD halt/resume round-trip (~few ms) plus the random sleep decorrelate
consecutive samples across the frame, giving a uniform statistical profile.

Usage: eb_sample.py [N_samples] [out_file]
Leaves the target running on exit.
"""
import socket, time, random, sys, re

HOST, PORT = "127.0.0.1", 6666
N   = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
OUT = sys.argv[2] if len(sys.argv) > 2 else "pcs.txt"

def cmd(s, c):
    s.sendall(c.encode() + b"\x1a")
    buf = b""
    while not buf.endswith(b"\x1a"):
        d = s.recv(4096)
        if not d:
            break
        buf += d
    return buf[:-1].decode(errors="replace")

s = socket.create_connection((HOST, PORT))
pcs = []
t0 = time.time()
try:
    for i in range(N):
        cmd(s, "halt")
        rp = cmd(s, "reg pc")
        rl = cmd(s, "reg lr")
        mp = re.search(r"0x[0-9a-fA-F]+", rp)
        ml = re.search(r"0x[0-9a-fA-F]+", rl)
        pcs.append((mp.group(0) if mp else "?") + " " + (ml.group(0) if ml else "?"))
        cmd(s, "resume")
        time.sleep(random.uniform(0.003, 0.025))
        if i % 100 == 0:
            print(f"  {i}/{N} ({time.time()-t0:.1f}s)", file=sys.stderr)
finally:
    try:
        cmd(s, "resume")   # never leave the game frozen
    except Exception:
        pass
    s.close()

with open(OUT, "w") as f:
    f.write("\n".join(pcs) + "\n")
good = sum(1 for p in pcs if p != "?")
print(f"wrote {good}/{len(pcs)} samples ({time.time()-t0:.1f}s) -> {OUT}", file=sys.stderr)
