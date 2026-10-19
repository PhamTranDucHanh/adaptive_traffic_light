#!/usr/bin/env python3
"""Resolve the latest version after a sphinx-multiversion build.

Scans the built HTML directory, picks the newest version by semantic
versioning, then writes:

  - <html_dir>/index.html  -> redirect to the latest version
  - <html_dir>/latest/     -> symlink (or copy) to the latest version

Usage:
    python tools/make_latest.py <html_dir>
    e.g.  python tools/make_latest.py build/html
"""

import os
import re
import sys
import shutil

# Folders that are never versions.
IGNORE = {"latest", "lastest", "stable", "_static", "_images",
          "_sources", "_downloads", ".doctrees"}

# Matches "v1.2.0", "1.2", "v1.2.0-rc1", etc. Group 1 = digits.
VERSION_RE = re.compile(r"^v?(\d+(?:\.\d+)*)([.\-_]?[A-Za-z].*)?$")

# Redirect page HTML lives in a real file next to this script (not in
# Sphinx's _templates/, to avoid clashing with the theme's Jinja2
# templates). Path is resolved relative to THIS script, so it works no
# matter which directory `make` is run from.
REDIRECT_TEMPLATE = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "templates", "redirect.html",
)

def parse(name):
    """Return (numbers_tuple, is_stable) or None if not a version."""
    m = VERSION_RE.match(name.strip())
    if not m:
        return None
    numbers = tuple(int(x) for x in m.group(1).split("."))
    is_stable = not m.group(2)  # no -rc/-beta suffix
    return numbers, is_stable

def find_versions(html_dir):
    """Return [(name, numbers, is_stable), ...] for valid version dirs."""
    result = []
    for name in os.listdir(html_dir):
        path = os.path.join(html_dir, name)
        if not os.path.isdir(path) or name in IGNORE or name.startswith("."):
            continue
        parsed = parse(name)
        if parsed:
            result.append((name, parsed[0], parsed[1]))
    return result

def pick_latest(versions, prefer_stable=True):
    """Pick the newest version. Stable wins over pre-release if present."""
    stable = [v for v in versions if v[2]]
    pool = stable if (prefer_stable and stable) else versions
    return max(pool, key=lambda v: (v[1], v[2]))[0]

def write_index(html_dir, latest):
    """Write index.html that redirects to the latest version.

    Reads the redirect markup from REDIRECT_TEMPLATE and substitutes
    the __TARGET__ placeholder, so the HTML stays editable on its own.
    Falls back to a minimal meta-refresh if the template is missing.
    """
    target = "./{}/index.html".format(latest)
    if os.path.isfile(REDIRECT_TEMPLATE):
        with open(REDIRECT_TEMPLATE, encoding="utf-8") as f:
            html = f.read().replace("__TARGET__", target)
    else:
        print("Warning: {} not found, used minimal fallback"
              .format(REDIRECT_TEMPLATE))
        html = (
            '<!DOCTYPE html><html lang="en"><head>'
            '<meta charset="utf-8">'
            '<meta http-equiv="refresh" content="0; url={t}">'
            '<link rel="canonical" href="{t}">'
            '<title>Redirecting...</title>'
            '<script>window.location.replace("{t}");</script>'
            '</head><body>'
            '<p><a href="{t}">Go to the latest documentation</a>.</p>'
            '</body></html>'
        ).format(t=target)
    with open(os.path.join(html_dir, "index.html"), "w",
              encoding="utf-8") as f:
        f.write(html)

def make_latest_dir(html_dir, latest):
    """Create <html_dir>/latest as a real copy of the latest version.
    """
    dst = os.path.join(html_dir, "latest")
    if os.path.islink(dst):
        os.unlink(dst)
    elif os.path.isdir(dst):
        shutil.rmtree(dst)
    elif os.path.exists(dst):
        os.remove(dst)

    shutil.copytree(os.path.join(html_dir, latest), dst)

def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: python tools/make_latest.py <html_dir>")

    html_dir = sys.argv[1]
    if not os.path.isdir(html_dir):
        sys.exit("Error: '{}' is not a directory".format(html_dir))

    versions = find_versions(html_dir)
    if not versions:
        sys.exit("Error: no version folders found in '{}'".format(html_dir))

    latest = pick_latest(versions)
    write_index(html_dir, latest)
    make_latest_dir(html_dir, latest)

    names = sorted(versions, key=lambda v: (v[1], v[2]), reverse=True)
    print("Versions : {}".format(", ".join(v[0] for v in names)))
    print("Latest   : {}".format(latest))
    print("index    : redirect -> {}/index.html".format(latest))
    print("latest/  : copy -> {}".format(latest))

if __name__ == "__main__":
    main()