#!/usr/bin/env python3
"""
tools/build64.py -- the 64-bit game program, both stages (docs/lp64.md,
"The wasm32 configuration's architecture").

    python tools/build64.py --build-dir build64 [--release]
        [--aletheia-root DIR] [--data-root DIR] [--sdl3-prefix DIR]
        [--cc HOST-CC] [--relay URL] [--fetch] [--fast] [--parts N]
        [--host-jobs N]

--fast is for testing: the generated C at -O1, in 96 smaller files, 8
compiled at once.  Releases keep the default (-O2, 32 files, 4 at once).

Stage one configures this project for wasm32 with wasi-sdk and builds the
module (BUILD/module/eidos_boot.wasm).  Stage two configures host/ with the
host's C compiler and builds the program (BUILD/host/eidos[.exe]) from the
module: wasm-opt, wasm2c, the generated C, wasm2c's runtime, host/ and SDL3.

The toolchain comes from tools/fetch_wasm.py's cache (--fetch fetches it
first); SDL3 from --sdl3-prefix, else tools/fetch_sdl.py's cache (MinGW on
Windows), else the system's (pkg-config's prefix).  Standard library only.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The game's C relies on wrapping arithmetic and loose aliasing, as the DS
# compiler gave it (the native build's flags, docs/building.md).
MODULE_FLAGS = "-O2 -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -D_GNU_SOURCE"

# Stream A (build lock, NXCPU-1): single-source point. CI test configures
# extract MODULE_FLAGS via --print-module-flags instead of hand copies.
# Stage two (the host/generated C) appends the same aliasing subset: the
# game relies on wrapping arithmetic and loose aliasing there too.
ALIAS_FLAGS = " ".join(t for t in MODULE_FLAGS.split()
                       if t in ("-fno-strict-aliasing", "-fwrapv",
                                "-fno-delete-null-pointer-checks"))

# Run-30 fixed-build module: 14481711 bytes (logs/30/checklist.txt). The -O0
# module was 27.7MB: 18MB passes the fixed build, fails an -O0 regression.
WASM_SIZE_CAP = 18 * 1024 * 1024

_O0_RE = re.compile(r"(?:^|\s)-O0(?:\s|$)")


def _read_text(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def assert_flags(build_dir, expect, artifact=None, cap_bytes=None, label="artifact"):
    """Fail closed (exit 1) unless the configured build carries every token
    of expect and nowhere compiles at -O0 (NXCPU-1: a file left at -O0 can
    cost milliseconds per frame). build.ninja holds the compile lines,
    CMakeCache.txt the configured CMAKE_C_FLAGS, so a hand
    -DCMAKE_C_FLAGS=-D_GNU_SOURCE (which drops -O2 and the rest) fails here.
    This path reads two text files and hashes one artifact: no GL calls
    anywhere near it (NXGL-4)."""
    if not expect:
        sys.exit("build64 --assert-flags: --expect is required")
    ninja = os.path.join(build_dir, "build.ninja")
    cache = os.path.join(build_dir, "CMakeCache.txt")
    for p in (ninja, cache):
        if not os.path.isfile(p):
            sys.exit("build64 --assert-flags: no %s (configure first)" % p)
    ninja_text = _read_text(ninja)
    cache_text = _read_text(cache)
    missing = [t for t in expect.split()
               if t not in ninja_text or t not in cache_text]
    if missing:
        sys.exit("build64 --assert-flags: %s lacks %s"
                 % (build_dir, " ".join(missing)))
    if _O0_RE.search(ninja_text) or _O0_RE.search(cache_text):
        sys.exit("build64 --assert-flags: %s contains -O0 (NXCPU-1)" % build_dir)
    print("build64 --assert-flags: %s carries '%s', no -O0"
          % (build_dir, expect), flush=True)
    if artifact is not None:
        report_artifact(artifact, cap_bytes, label)
    return 0


def report_artifact(path, cap_bytes, label):
    """Print '<label>: <bytes> bytes, sha256 <hex>' (the round-3 deliverable
    format) and fail closed when the artifact exceeds its cap."""
    h = hashlib.sha256()
    n = 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
            n += len(chunk)
    print("%s: %d bytes, sha256 %s" % (label, n, h.hexdigest()), flush=True)
    if cap_bytes is not None and n > cap_bytes:
        sys.exit("%s: %d bytes exceeds cap %d bytes" % (label, n, cap_bytes))
    return 0


def run(args, **kw):
    print("+ " + " ".join(args), flush=True)
    subprocess.check_call(args, **kw)


def tool_paths(fetch):
    cmd = [sys.executable, os.path.join(ROOT, "tools", "fetch_wasm.py")]
    if not fetch:
        cmd.append("--locate")
    p = subprocess.run(cmd, capture_output=True, text=True)
    if fetch:
        sys.stdout.write(p.stdout)
        sys.stderr.write(p.stderr)
    paths = dict(line.split("=", 1) for line in p.stdout.splitlines() if "=" in line)
    need = ("WASI_SDK_PREFIX", "WASM_OPT", "WABT_ROOT", "WASM2C")
    missing = [k for k in need if k not in paths]
    if missing:
        sys.exit("build64: no %s; run python tools/fetch_wasm.py (or pass --fetch)"
                 % ", ".join(missing))
    return paths


def sdl3_prefix(arg):
    if arg:
        return arg
    if sys.platform.startswith("win"):
        p = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "fetch_sdl.py"),
                            "--locate", "mingw", "--arch", "x64"],
                           capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip():
            return p.stdout.strip().splitlines()[-1]
        sys.exit("build64: no SDL3 for MinGW x64; run python tools/fetch_sdl.py --flavor mingw")
    p = subprocess.run(["pkg-config", "--variable=prefix", "sdl3"], capture_output=True, text=True)
    if p.returncode == 0 and p.stdout.strip():
        return p.stdout.strip()
    sys.exit("build64: no SDL3 found; pass --sdl3-prefix")


def main():
    if "--print-module-flags" in sys.argv:
        # No --build-dir needed: workflows source the flags from here
        # instead of keeping hand copies.
        print(MODULE_FLAGS)
        return 0
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--release", action="store_true", help="EIDOS_RELEASE=ON in stage one")
    ap.add_argument("--aletheia-root")
    ap.add_argument("--data-root")
    ap.add_argument("--sdl3-prefix")
    ap.add_argument("--cc", help="the host's C compiler for stage two")
    ap.add_argument("--relay", default=None, help="EIDOS_DEFAULT_RELAY")
    ap.add_argument("--host-opt", default="-O2", help="wasm-opt's level (EIDOS_HOST_OPT)")
    ap.add_argument("--fetch", action="store_true", help="fetch the toolchain first")
    ap.add_argument("--fast", action="store_true",
                    help="a quick build for testing: the generated C at -O1, in "
                         "more, smaller files, more of them at once (releases "
                         "keep -O2)")
    ap.add_argument("--parts", type=int, default=None,
                    help="how many C files wasm2c writes (EIDOS_HOST_PARTS); "
                         "smaller files need less memory each")
    ap.add_argument("--host-jobs", type=int, default=None,
                    help="stage two's parallel compiles: each generated file takes "
                         "1-2 GB to compile")
    ap.add_argument("--print-module-flags", action="store_true",
                    help="print MODULE_FLAGS and exit (CI single-sources flags from here)")
    ap.add_argument("--assert-flags", action="store_true",
                    help="grep --build-dir's build.ninja/CMakeCache.txt for --expect "
                         "tokens, refuse -O0, report --artifact (bytes+sha256) "
                         "against --cap-bytes; exit 1 on any failure")
    ap.add_argument("--expect", default=None,
                    help="space-separated flag tokens --assert-flags requires")
    ap.add_argument("--artifact", default=None,
                    help="file --assert-flags reports and checks against --cap-bytes")
    ap.add_argument("--cap-bytes", type=int, default=None,
                    help="maximum --artifact size in bytes")
    ap.add_argument("--label", default="artifact",
                    help="label for the --artifact report line")
    a = ap.parse_args()
    if a.assert_flags:
        return assert_flags(a.build_dir, a.expect, a.artifact,
                            a.cap_bytes, a.label)
    # the release build: 32 files of 1-2 GB each to compile, 4 at a time;
    # the quick one: 96 smaller files at -O1, which need much less memory
    # each, so 8 at a time
    parts = a.parts if a.parts is not None else (96 if a.fast else 32)
    jobs = a.host_jobs if a.host_jobs is not None else (8 if a.fast else 4)

    t = tool_paths(a.fetch)
    sdl = sdl3_prefix(a.sdl3_prefix)
    build = os.path.abspath(a.build_dir)
    mod, host = os.path.join(build, "module"), os.path.join(build, "host")
    gen = ["-G", "Ninja"] if shutil.which("ninja") else []

    # stage one: the module
    sdk = t["WASI_SDK_PREFIX"]
    args = ["cmake", "-S", ROOT, "-B", mod] + gen + [
        "-DCMAKE_TOOLCHAIN_FILE=%s/share/cmake/wasi-sdk-p1.cmake" % sdk,
        "-DWASI_SDK_PREFIX=%s" % sdk,
        "-DCMAKE_C_FLAGS=%s" % MODULE_FLAGS,
        "-DEIDOS_BOOT=ON", "-DEIDOS_BOOT_ALL_GAME=ON", "-DEIDOS_OVERLAYS=ALL",
        # the relay's TLS (cmake/mbedtls_wasm.h) and the code mods' runtime
        # (cmake/wamr/wasi) are in the module
        "-DEIDOS_TLS=ON", "-DEIDOS_WASM=ON",
        "-DEIDOS_RELEASE=%s" % ("ON" if a.release else "OFF"),
        # SDL3's headers (the module's SDL calls are the host's imports):
        # cmake/sdl3.cmake takes them from fetch_sdl.py's MinGW package
        "-DEIDOS_SDL_FETCH=ON"]
    # Mbed TLS's and WAMR's sources, where their fetch scripts keep them
    # (the module's defaults would look under $HOME, which a Windows host
    # lacks); each script's last line is the folder
    for script, var in (("fetch_mbedtls.py", "EIDOS_MBEDTLS_ROOT"),
                        ("fetch_wamr.py", "EIDOS_WAMR_ROOT")):
        p = subprocess.run([sys.executable, os.path.join(ROOT, "tools", script)],
                           capture_output=True, text=True)
        if p.returncode == 0 and p.stdout.strip():
            args.append("-D%s=%s" % (var, p.stdout.strip().splitlines()[-1]))
    if a.aletheia_root:
        args.append("-DALETHEIA_ROOT=%s" % a.aletheia_root)
    if a.data_root:
        args.append("-DBW_DATA_ROOT=%s" % a.data_root)
    if a.relay is not None:
        args.append("-DEIDOS_DEFAULT_RELAY=%s" % a.relay)
    run(args)
    run(["cmake", "--build", mod, "--target", "eidos_boot"])
    assert_flags(mod, MODULE_FLAGS,
                 os.path.join(mod, "eidos_boot.wasm"),
                 WASM_SIZE_CAP, "module eidos_boot.wasm")

    # stage two: the program. The generated/host C gets the same
    # wrapping/aliasing semantics as the module (NXCPU-1, ALIAS_FLAGS above);
    # the -O level stays -O2 (releases) or -O1 (--fast): no -O3 experiment.
    stage_two_flags = "%s %s" % (("-O1" if a.fast else "-O2"), ALIAS_FLAGS)
    args = ["cmake", "-S", os.path.join(ROOT, "host"), "-B", host] + gen + [
        "-DEIDOS_WASM_MODULE=%s" % os.path.join(mod, "eidos_boot.wasm"),
        "-DWABT_ROOT=%s" % t["WABT_ROOT"], "-DWASM2C=%s" % t["WASM2C"],
        "-DWASM_OPT=%s" % t["WASM_OPT"], "-DSDL3_PREFIX=%s" % sdl,
        "-DEIDOS_HOST_OPT=%s" % a.host_opt,
        "-DCMAKE_C_FLAGS=%s" % stage_two_flags,
        "-DEIDOS_HOST_PARTS=%d" % parts,
        "-DEIDOS_HOST_WINDOWED=%s" % ("ON" if a.release else "OFF")]
    if a.cc:
        args.append("-DCMAKE_C_COMPILER=%s" % a.cc)
    run(args)
    run(["cmake", "--build", host, "--parallel", str(jobs)])
    assert_flags(host, stage_two_flags)
    return 0


if __name__ == "__main__":
    sys.exit(main())
