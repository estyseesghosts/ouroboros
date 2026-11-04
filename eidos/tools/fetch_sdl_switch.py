#!/usr/bin/env python3
"""
tools/fetch_sdl_switch.py -- devkitPro's SDL3 fork for the Switch build.

The official SDL3 Switch port is behind Nintendo's NDA.  devkitPro's fork
has a `switch-sdl-3.4` branch whose video driver is EGL over Mesa, with
audio, gamepads, touch and the software keyboard (switch-plan.md, S1).
This script downloads one pinned commit of it, checks the archive's
SHA-256, unpacks it outside the repository and prints the source folder as
its last line, the way the Android and iOS builds expect.

    python3 tools/fetch_sdl_switch.py [--cache DIR]

The cache is --cache, else EIDOS_SDL_SWITCH_CACHE, else
~/.cache/eidos/sdl-switch.  Standard library only.
"""

import argparse
import hashlib
import os
import shutil
import sys
import tarfile
import urllib.request

COMMIT = "c329016c0e338ab4397ad930f83330c9fe058348"
SHA256 = "6047e4787f105ab4a0c6293ee3090ce5d1ebad112fcbe2a3e5a8874b0cc6f709"
URL = "https://github.com/devkitPro/SDL/archive/%s.tar.gz" % COMMIT


def cache_dir(arg):
    if arg:
        return os.path.abspath(arg)
    env = os.environ.get("EIDOS_SDL_SWITCH_CACHE")
    if env:
        return os.path.abspath(env)
    base = os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache")
    return os.path.join(base, "eidos", "sdl-switch")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--cache", help="where downloads and the source tree live")
    a = ap.parse_args()

    cache = cache_dir(a.cache)
    os.makedirs(cache, exist_ok=True)
    archive = os.path.join(cache, "SDL-%s.tar.gz" % COMMIT)
    folder = os.path.join(cache, "SDL-%s" % COMMIT)
    stamp = os.path.join(folder, ".eidos-sha256")

    if os.path.exists(stamp) and open(stamp).read().strip() == SHA256:
        print("SDL3 %s ready in %s" % (COMMIT, folder))
        return 0

    if not (os.path.exists(archive) and sha256(archive) == SHA256):
        print("fetch_sdl_switch: downloading %s" % URL)
        tmp = archive + ".part"
        with urllib.request.urlopen(URL) as r, open(tmp, "wb") as f:
            shutil.copyfileobj(r, f)
        got = sha256(tmp)
        if got != SHA256:
            os.remove(tmp)
            sys.exit("fetch_sdl_switch: SHA-256 %s, expected %s" % (got, SHA256))
        os.replace(tmp, archive)

    if os.path.exists(folder):
        shutil.rmtree(folder)
    tmpdir = folder + ".part"
    if os.path.exists(tmpdir):
        shutil.rmtree(tmpdir)
    os.makedirs(tmpdir)
    with tarfile.open(archive) as t:
        try:
            t.extractall(tmpdir, filter="data")
        except TypeError:                      # Python before 3.12
            t.extractall(tmpdir)
    top = os.path.join(tmpdir, os.listdir(tmpdir)[0])
    os.replace(top, folder)
    os.rmdir(tmpdir)
    with open(stamp, "w", encoding="utf-8") as f:
        f.write(SHA256 + "\n")
    print("SDL3 %s ready in %s" % (COMMIT, folder))
    return 0


if __name__ == "__main__":
    sys.exit(main())
