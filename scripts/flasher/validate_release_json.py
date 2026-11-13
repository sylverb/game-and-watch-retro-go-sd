#!/usr/bin/env python3
"""
Validate a firmware release's JSON against schema/firmware-*.schema.json.

Run over the committed fixtures:

    scripts/flasher/validate_release_json.py docs/examples

or over a real packer output directory:

    scripts/flasher/validate_release_json.py path/to/out

JSON Schema describes one field at a time. The cross-field rules a release also
has to satisfy live in check_cross_field() below, the same split
gwrg-dist-spec uses between its schema and site/check.js.
"""
import argparse
import json
import os
import sys

try:
    from jsonschema import Draft202012Validator
except ImportError:
    print(
        "error: jsonschema is not installed (pip install jsonschema)",
        file=sys.stderr,
    )
    sys.exit(2)

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA_DIR = os.path.join(HERE, "..", "..", "schema")

DOCS = (
    ("manifest.json", "firmware-manifest.schema.json"),
    ("versions.json", "firmware-versions.schema.json"),
    ("projects.json", "firmware-projects.schema.json"),
)


def check_cross_field(manifest, versions, problems):
    """Rules that span more than one field, which a schema cannot express."""
    ids = [b["id"] for b in manifest["builds"]]
    if len(set(ids)) != len(ids):
        problems.append(f"manifest: duplicate build ids in {ids}")

    # Every language a build ships must be declared at the top level, and every
    # declared language except en_us must be shipped by some build. en_us is
    # baked into rodata and has no blob.
    shipped = {
        item["language"]
        for b in manifest["builds"]
        for item in b["content"]
        if "language" in item
    }
    declared = set(manifest["languages"])
    for lang in shipped - declared:
        problems.append(f"manifest: build ships {lang} but languages[] omits it")
    for lang in declared - shipped - {"en_us"}:
        problems.append(f"manifest: languages[] declares {lang} but no build ships it")

    # A build's declared members must not collide on where they land on device.
    for b in manifest["builds"]:
        installs = [item["install"] for item in b["content"]]
        dupes = {p for p in installs if installs.count(p) > 1}
        if dupes:
            problems.append(f"manifest: {b['id']} installs {sorted(dupes)} more than once")

    # sdUpdate may share the image's zip entry, but only when it is the same bytes.
    for b in manifest["builds"]:
        upd = b.get("sdUpdate")
        if upd and upd["path"] == b["image"]["path"] and upd["sha256"] != b["image"]["sha256"]:
            problems.append(
                f"manifest: {b['id']} sdUpdate shares the image's zip entry "
                "but declares a different sha256"
            )

    if versions is None:
        return

    if versions["project"] != manifest["project"]:
        problems.append("versions/manifest: project mismatch")

    # The index duplicates three fields so a picker can filter before fetching a
    # manifest. They have to agree; the manifest wins.
    entry = next(
        (v for v in versions["versions"] if v["manifest"].endswith("manifest.json")),
        None,
    )
    if entry:
        for field, got in (
            ("gitTag", entry["gitTag"]),
            ("providesAbi", entry["providesAbi"]),
            ("coreMetaVersion", entry["coreMetaVersion"]),
        ):
            want = manifest["firmware"][field]
            if got != want:
                problems.append(
                    f"versions[0].{field} {got!r} disagrees with manifest {want!r}"
                )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("directory", help="directory holding the release JSON")
    args = ap.parse_args()

    problems = []
    loaded = {}

    for name, schema_name in DOCS:
        path = os.path.join(args.directory, name)
        schema_path = os.path.join(SCHEMA_DIR, schema_name)
        if not os.path.isfile(path):
            if name == "projects.json":
                continue  # optional
            problems.append(f"{name}: missing")
            continue

        with open(schema_path, encoding="utf-8") as f:
            schema = json.load(f)
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        loaded[name] = doc

        validator = Draft202012Validator(schema)
        errors = sorted(validator.iter_errors(doc), key=lambda e: list(e.absolute_path))
        for e in errors:
            where = "/".join(str(p) for p in e.absolute_path) or "(root)"
            problems.append(f"{name}: {where}: {e.message}")
        if not errors:
            print(f"ok  {name:<14} validates against {schema_name}")

    if "manifest.json" in loaded:
        check_cross_field(loaded["manifest.json"], loaded.get("versions.json"), problems)

    if problems:
        print()
        for p in problems:
            print(f"FAIL {p}")
        print(f"\n{len(problems)} problem(s)")
        return 1

    print("ok  cross-field checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
