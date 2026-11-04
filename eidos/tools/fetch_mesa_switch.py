#!/usr/bin/env python3
"""
tools/fetch_mesa_switch.py -- mesa-switch GL SDK for the Switch build.

danfromtico/mesa-switch provides EGL and OpenGL/GLES for the Switch GM20B
through Gallium NVC0 (the default) or Zink-on-NVK. This script pins one
commit of it, reuses a cache outside the repository, and prints the
installed prefix as its last line, the way the SDL fetch does.

    python3 tools/fetch_mesa_switch.py [--cache DIR] [--build]

The cache is --cache, else EIDOS_MESA_SWITCH_CACHE, else
~/.cache/eidos/mesa-switch. The layout is:

    <cache>/<commit>/builddir-opengl/   Meson build tree (A1)
    <cache>/<commit>/mesa-install/      DESTDIR staging (A1)
    <cache>/<commit>/mesa-install/opt/devkitpro/portlibs/switch/
                                        the prefix (lib/, include/)

With a populated cache the script prints the prefix and does nothing else:
no network, no build. With --build and an empty cache it runs mesa's
build-opengl.sh from a mesa-switch checkout (EIDOS_MESA_SWITCH_SOURCE, else
the sibling ../../mesa-switch next to this repository). Without --build and
an empty cache it exits non-zero and tells how to build.

Only the pinned SHA is ever used; there are no floating refs.
Standard library only.
"""

import argparse
import os
import subprocess
import sys

COMMIT = "16732f61427b795abf37e40d957cb62213caec8a"

REQUIRED = [
    "lib/libEGL.a",
    "lib/libGLESv2.a",
    "lib/pkgconfig/egl.pc",
    "lib/pkgconfig/glesv2.pc",
    "include/EGL/egl.h",
]


def cache_dir(arg):
    if arg:
        return os.path.abspath(arg)
    env = os.environ.get("EIDOS_MESA_SWITCH_CACHE")
    if env:
        return os.path.abspath(env)
    base = os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache")
    return os.path.join(base, "eidos", "mesa-switch")


def mesa_source():
    env = os.environ.get("EIDOS_MESA_SWITCH_SOURCE")
    if env:
        return os.path.abspath(env)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sib = os.path.abspath(os.path.join(root, "..", "mesa-switch"))
    if os.path.isfile(os.path.join(sib, "build-opengl.sh")):
        return sib
    return None


def prefix_for(cache):
    return os.path.join(cache, COMMIT, "mesa-install",
                         "opt", "devkitpro", "portlibs", "switch")


def is_ready(prefix):
    stamp = os.path.join(prefix, ".eidos-mesa-sha")
    if not (os.path.isfile(stamp) and
            open(stamp, encoding="utf-8").read().strip() == COMMIT):
        return False
    return all(os.path.isfile(os.path.join(prefix, r)) for r in REQUIRED)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--cache", help="where the SDK trees live")
    ap.add_argument("--build", action="store_true",
                    help="run mesa's build-opengl.sh when the cache is empty")
    a = ap.parse_args()

    cache = cache_dir(a.cache)
    prefix = prefix_for(cache)

    if is_ready(prefix):
        print("mesa-switch %s ready in %s" % (COMMIT, prefix))
        return 0

    if not a.build:
        sys.exit("fetch_mesa_switch: no SDK at %s; "
                 "run with --build (needs devkitPro Linux or Docker, "
                 "see switch-mesa/sdk-build.md)" % prefix)

    src = mesa_source()
    if src is None:
        sys.exit("fetch_mesa_switch: no mesa-switch checkout; set "
                 "EIDOS_MESA_SWITCH_SOURCE to one at commit %s" % COMMIT)
    try:
        head = subprocess.check_output(
            ["git", "-C", src, "rev-parse", "HEAD"], text=True).strip()
    except (subprocess.CalledProcessError, OSError):
        sys.exit("fetch_mesa_switch: cannot read HEAD of %s" % src)
    if head != COMMIT:
        sys.exit("fetch_mesa_switch: checkout at %s is %s, expected %s" %
                 (src, head, COMMIT))
    if subprocess.call(["git", "-C", src, "status", "--porcelain"]) != 0:
        sys.exit("fetch_mesa_switch: cannot inspect %s" % src)
    dirty = subprocess.check_output(
        ["git", "-C", src, "status", "--porcelain"], text=True).strip()
    if dirty:
        sys.exit("fetch_mesa_switch: refusing a dirty mesa checkout (A1 "
                 "forbids ALLOW_DIRTY=1)")

    build_dir = os.path.join(cache, COMMIT, "builddir-opengl")
    destdir = os.path.join(cache, COMMIT, "mesa-install")
    os.makedirs(cache, exist_ok=True)
    env = dict(os.environ)
    env["BUILD_DIR"] = build_dir
    env["DESTDIR"] = destdir
    print("fetch_mesa_switch: building %s into %s" % (COMMIT, prefix),
          flush=True)
    subprocess.check_call(["bash", "build-opengl.sh"], cwd=src, env=env)

    if not is_ready(prefix):
        # build-opengl.sh writes the stamp only through this helper, so a
        # successful build without one is still a miss until stamped.
        missing = [r for r in REQUIRED
                   if not os.path.isfile(os.path.join(prefix, r))]
        sys.exit("fetch_mesa_switch: build finished but %s is incomplete; "
                 "missing %s" % (prefix, ", ".join(missing)))
    with open(os.path.join(prefix, ".eidos-mesa-sha"), "w",
              encoding="utf-8") as f:
        f.write(COMMIT + "\n")
    print("mesa-switch %s ready in %s" % (COMMIT, prefix))
    return 0


if __name__ == "__main__":
    sys.exit(main())
