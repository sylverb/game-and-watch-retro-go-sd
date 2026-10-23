#!/usr/bin/env python3
"""
Generate projects.json — the curated list of cores and homebrew a browser
installer can offer alongside a firmware release — from docs/CURATED_PROJECTS.md.

The markdown file is the source of truth so the list is reviewable in a diff.
This script is the only thing that reads it, and it refuses to emit a partial
file: a malformed row is a broken release, not a shorter list.

Output shape:

    {
      "schemaVersion": 1,
      "projects": [
        { "project": "wsv", "title": "Potator", "kind": "core",
          "versionsUrl": "https://slash-proc.github.io/potator-retro-go-sd/dist/versions.json" }
      ]
    }

`kind` is "core" or "homebrew" — matching the projects' own PROJECT_KIND, not
the dist spec's "emulator"/"homebrew". `versionsUrl` is derived from the repo
slug: a project's Pages mirror always serves dist/versions.json (see
gwrg-dist-spec spec/01-distribution.md), so the markdown carries the repo and
this script carries the shape.
"""
import argparse
import json
import os
import re
import sys

SCHEMA_VERSION = 1

# `## Emulator cores` / `## Homebrew` select the kind for the rows beneath them.
# Any other heading (e.g. `## Not listed`) stops row collection until the next
# recognised one — that is how the trailing prose section is skipped without
# needing to know what it says.
SECTION_KINDS = {
    "emulator cores": "core",
    "homebrew": "homebrew",
}

PAGES_URL = "https://slash-proc.github.io/{repo_name}/dist/versions.json"

HEADING_RE = re.compile(r"^##\s+(.*?)\s*$")
# A table row: leading pipe, cells, trailing pipe. Separator rows (---|---) are
# filtered out by the cell content check rather than a second regex.
ROW_RE = re.compile(r"^\|(.+)\|\s*$")
# Values arrive as `code` spans in the markdown; strip the backticks.
CODE_RE = re.compile(r"^`(.+)`$")

# The project cell is a linked slug: [`nes-fceu`](https://github.com/owner/name).
# The link is the only place the repository is named — there is no separate Repo
# column — so a row that is not a link is malformed rather than merely unlinked.
LINK_RE = re.compile(r"^\[`([^`]+)`\]\(https://github\.com/([^)\s]+)\)$")

PROJECT_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
REPO_RE = re.compile(r"^[^/\s]+/[^/\s]+$")


class Malformed(Exception):
    pass


def _cells(line):
    m = ROW_RE.match(line)
    if not m:
        return None
    return [c.strip() for c in m.group(1).split("|")]


def _unwrap_code(value, field, lineno):
    m = CODE_RE.match(value)
    if not m:
        raise Malformed(
            f"line {lineno}: {field} must be a `code` span, got {value!r}"
        )
    return m.group(1).strip()


def parse(text):
    """Parse the curated-projects markdown into a list of project dicts.

    Raises Malformed on anything it cannot read cleanly. Every table under a
    recognised heading must have Project as column 0, Title as column 1 and Repo
    as the LAST column — the middle columns differ between the two tables
    (Systems vs From) and are documentation, not data.
    """
    projects = []
    seen = {}
    kind = None

    for lineno, line in enumerate(text.splitlines(), start=1):
        heading = HEADING_RE.match(line)
        if heading:
            kind = SECTION_KINDS.get(heading.group(1).strip().lower())
            continue

        if kind is None:
            continue

        cells = _cells(line)
        if cells is None:
            continue

        # Header row and separator row of each table.
        if cells[0].lower() == "project":
            continue
        if set("".join(cells)) <= set("-: "):
            continue

        if len(cells) < 3:
            raise Malformed(
                f"line {lineno}: expected at least 3 columns, got {len(cells)}: {line!r}"
            )

        link = LINK_RE.match(cells[0])
        if not link:
            raise Malformed(
                f"line {lineno}: project cell {cells[0]!r} is not a linked slug "
                "of the form [`slug`](https://github.com/owner/name)"
            )
        project, repo = link.group(1), link.group(2).rstrip("/")
        title = cells[1].strip()

        if not PROJECT_RE.match(project):
            raise Malformed(
                f"line {lineno}: project slug {project!r} is not [a-z0-9][a-z0-9-]*"
            )
        if not title:
            raise Malformed(f"line {lineno}: empty title for {project!r}")
        if not REPO_RE.match(repo):
            raise Malformed(
                f"line {lineno}: repo {repo!r} is not owner/name"
            )
        if project in seen:
            raise Malformed(
                f"line {lineno}: duplicate project {project!r} "
                f"(first seen on line {seen[project]})"
            )
        seen[project] = lineno

        projects.append(
            {
                "project": project,
                "title": title,
                "kind": kind,
                "versionsUrl": PAGES_URL.format(repo_name=repo.split("/", 1)[1]),
            }
        )

    if not projects:
        raise Malformed(
            "no projects found — expected tables under '## Emulator cores' "
            "and/or '## Homebrew'"
        )
    return projects


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", default="docs/CURATED_PROJECTS.md")
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    with open(args.input, encoding="utf-8") as f:
        text = f.read()

    try:
        projects = parse(text)
    except Malformed as e:
        print(f"{args.input}: {e}", file=sys.stderr)
        return 1

    doc = {"schemaVersion": SCHEMA_VERSION, "projects": projects}
    out_dir = os.path.dirname(os.path.abspath(args.output))
    os.makedirs(out_dir, exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2, ensure_ascii=False)
        f.write("\n")

    cores = sum(1 for p in projects if p["kind"] == "core")
    print(
        f"{args.output}: {len(projects)} projects "
        f"({cores} cores, {len(projects) - cores} homebrew)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
