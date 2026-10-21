#!/usr/bin/env python3
"""Describes a release package for the app's updater.

Writes manifest.json with the SHA-256 of every file in the package. When the
previous release's manifest is given, also copies the files that changed
since then into a patch folder; the updater downloads that small patch
instead of the whole package when the installed copy is the previous
version.
"""
import argparse
import hashlib
import json
import os
import shutil


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", required=True, help="folder with the app (LumoraStudio.exe at its top)")
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True, help="manifest.json to write")
    ap.add_argument("--previous", help="manifest.json of the previous release")
    ap.add_argument("--patch-dir", help="folder that receives LumoraStudio/<changed files>")
    args = ap.parse_args()

    files = {}
    for root, _, names in os.walk(args.package):
        for name in names:
            path = os.path.join(root, name)
            files[os.path.relpath(path, args.package).replace(os.sep, "/")] = sha256(path)
    manifest = {"version": args.version, "files": dict(sorted(files.items()))}

    if args.previous and args.patch_dir and os.path.exists(args.previous):
        with open(args.previous, encoding="utf-8") as f:
            previous = json.load(f)
        old = previous.get("files", {})
        changed = sorted(p for p, h in files.items() if old.get(p) != h)
        # The updater finds the package root by the executable, so it is always in.
        if "LumoraStudio.exe" in files and "LumoraStudio.exe" not in changed:
            changed.insert(0, "LumoraStudio.exe")
        for p in changed:
            dest = os.path.join(args.patch_dir, "LumoraStudio", p)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copy2(os.path.join(args.package, p), dest)
        manifest["patch"] = {"from": previous.get("version", ""), "files": changed}
        print(f"Patch from {manifest['patch']['from']}: {len(changed)} of {len(files)} files")

    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)


if __name__ == "__main__":
    main()
