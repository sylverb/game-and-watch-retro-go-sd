#!/usr/bin/env python3
"""Write a minimal index.html for the Pages firmware mirror.

The mirror is a file tree for tools (CORS). Without an index, directory URLs
404 in the browser; this page lists the retained versions and links into
dist/<tag>/ so humans can poke around.
"""

from __future__ import annotations

import argparse
import html
import json
import os
from pathlib import Path


PAGE = """\
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
  :root {{ color-scheme: light dark; }}
  body {{ font-family: system-ui, sans-serif; margin: 2rem auto; max-width: 48rem;
         line-height: 1.45; padding: 0 1rem; }}
  h1 {{ font-size: 1.4rem; }}
  code, a {{ word-break: break-all; }}
  ul {{ padding-left: 1.2rem; }}
  .muted {{ opacity: 0.75; font-size: 0.9rem; }}
</style>
</head>
<body>
<h1>{heading}</h1>
{body}
</body>
</html>
"""


def write_html(path: Path, title: str, heading: str, body: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        PAGE.format(title=html.escape(title), heading=html.escape(heading), body=body),
        encoding="utf-8",
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pages-root", required=True, help="Root of the Pages tree (_pages)")
    ap.add_argument("--repo", required=True, help="owner/repo")
    args = ap.parse_args()

    root = Path(args.pages_root)
    dist = root / "dist"
    versions_path = dist / "versions.json"
    if not versions_path.is_file():
        raise SystemExit(f"missing {versions_path}")

    versions = json.loads(versions_path.read_text(encoding="utf-8"))
    releases_url = versions.get("releasesUrl") or f"https://github.com/{args.repo}/releases"
    entries = versions.get("versions") or []

    version_items = []
    for v in entries:
        tag = v["tag"]
        label = html.escape(tag)
        if v.get("prerelease"):
            label += ' <span class="muted">(pre-release)</span>'
        version_items.append(
            f'<li><a href="dist/{html.escape(tag)}/">{label}</a>'
            f' — <a href="dist/{html.escape(tag)}/manifest.json"><code>manifest.json</code></a></li>'
        )

    root_body = f"""\
<p>Firmware distribution mirror for
<a href="https://github.com/{html.escape(args.repo)}"><code>{html.escape(args.repo)}</code></a>.
Tools should start at <a href="dist/versions.json"><code>dist/versions.json</code></a>
(CORS-friendly). Humans usually want the
<a href="{html.escape(releases_url)}">GitHub release</a>
(<code>retro-go_update.bin</code>).</p>
<p><a href="dist/versions.json"><code>dist/versions.json</code></a></p>
<h2>Mirrored versions</h2>
<ul>
{chr(10).join(version_items) if version_items else "<li class=\"muted\">(none)</li>"}
</ul>
"""
    write_html(root / "index.html", "Retro-Go SD — Pages mirror", "Retro-Go SD Pages mirror", root_body)

    dist_items = [
        f'<li><a href="{html.escape(tag)}/"><code>{html.escape(tag)}/</code></a></li>'
        for tag in (v["tag"] for v in entries)
    ]
    dist_body = f"""\
<p>Parent: <a href="../">site root</a> ·
<a href="versions.json"><code>versions.json</code></a></p>
<ul>
{chr(10).join(dist_items) if dist_items else "<li class=\"muted\">(none)</li>"}
</ul>
"""
    write_html(dist / "index.html", "dist/ — Retro-Go SD", "dist/", dist_body)

    for v in entries:
        tag = v["tag"]
        tag_dir = dist / tag
        if not tag_dir.is_dir():
            continue
        files = sorted(
            p.name for p in tag_dir.iterdir()
            if p.is_file() and p.name != "index.html"
        )
        file_items = [
            f'<li><a href="{html.escape(name)}"><code>{html.escape(name)}</code></a>'
            f' <span class="muted">({os.path.getsize(tag_dir / name):,} bytes)</span></li>'
            for name in files
        ]
        body = f"""\
<p>Parent: <a href="../">dist/</a> ·
<a href="../../">site root</a> ·
<a href="../versions.json"><code>versions.json</code></a></p>
<ul>
{chr(10).join(file_items) if file_items else "<li class=\"muted\">(empty)</li>"}
</ul>
"""
        write_html(
            tag_dir / "index.html",
            f"{tag} — Retro-Go SD",
            tag,
            body,
        )

    print(f"wrote index.html under {root} and {len(entries)} tag dir(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
