# mesa-switch SDK build (Phase A1)

GL-only SDK for eidos on Switch, from danfromtico/mesa-switch.

- SHA: `0ff52079ca9e9e837ed27eb95a8d239f439a1864`
  (`horizon: runtime shutdown also closes the devices' cached buffers`,
  2026-10-12)
- Host: macOS arm64 (Darwin 27.0.0), Docker 29.8.2, image built from
  `mesa-switch/Docker.mesa` (`FROM devkitpro/devkita64:latest` + Meson).
- No source edits in `mesa:`; clean checkout; no unified/Vulkan build;
  no `ALLOW_DIRTY=1`.

## Commands as run

```sh
cd /Users/terezi/bw-switch/src/mesa-switch
docker build -f Docker.mesa -t mesa-switch-build .
mkdir -p ~/.cache/eidos/mesa-switch
docker run --rm \
  -v $HOME/.cache/eidos/mesa-switch:/cache \
  -v $(pwd):/src -w /src \
  -e BUILD_DIR=/cache/0ff52079ca9e9e837ed27eb95a8d239f439a1864/builddir-opengl \
  -e DESTDIR=/cache/0ff52079ca9e9e837ed27eb95a8d239f439a1864/mesa-install \
  mesa-switch-build bash build-opengl.sh
```

`build-opengl.sh` runs Meson `--cross-file switch_cross_file.txt`,
`--prefix=/opt/devkitpro/portlibs/switch --libdir=lib
--default-library=static`, `-Dgallium-drivers=nouveau -Dplatforms=switch
-Degl-native-platform=switch -Degl=enabled -Dopengl=true -Dgles1/2=enabled`,
then `ninja` + `meson install --destdir`. Pkg-config prefixes rewritten to
`${pcfiledir}/../..`.

Equivalent helper:

```sh
python3 tools/fetch_mesa_switch.py --build
```

## Toolchain versions (inside `mesa-switch-build`)

- devkitA64 GCC 15.2.0 (`aarch64-none-elf-gcc --version`)
- Meson 1.12.1, Ninja 1.11.1
- libnx from `devkitpro/devkita64:latest` as pulled 2026-11-02
  (no separate version file in image; Switch portlibs present)
- Rust: not used for the GL-only build (no rustc in image; build succeeds)

## Installed prefix

`~/.cache/eidos/mesa-switch/0ff52079ca9e9e837ed27eb95a8d239f439a1864/mesa-install/opt/devkitpro/portlibs/switch/`
(66M)

Headers: `include/EGL/egl.h`, `include/GLES2/gl2.h`, `include/GLES3/gl32.h`,
`include/GL/gl.h`, `include/KHR/khrplatform.h`, etc.
Pkg-config: `lib/pkgconfig/{egl,gl,glapi,glesv1_cm,glesv2}.pc`.
CMake: `lib/cmake/OpenGL/OpenGLConfig.cmake` exporting `OpenGL::EGL`,
`OpenGL::GL`, `OpenGL::GLES1`, `OpenGL::GLES2`.

## Static archives (bytes)

| File | Size |
| --- | --- |
| `lib/libEGL.a` | 42542146 |
| `lib/libGL.a` | 578884 |
| `lib/libGLESv1_CM.a` | 63860 |
| `lib/libGLESv2.a` | 161126 |
| `lib/libglapi.a` | 1912644 |
| `lib/libblake3.a` | 34646 |
| `lib/libcompiler.a` | 241724 |
| `lib/libmesa_util_c11.a` | 13892 |
| `lib/libmesa_util_simd.a` | 1932 |
| `lib/libmesa_util.a` | 4887898 |
| `lib/libnir.a` | 11601150 |
| `lib/libnouveau_horizon.a` | 138294 |
| `lib/libnouveau_ws.a` | 2061474 |
| `lib/libnvidia_headers_c.a` | 2058042 |
| `lib/libvtn.a` | 860344 |
| `lib/libxmlconfig.a` | 38612 |

Stamp: `<prefix>/.eidos-mesa-sha` contains the SHA.
`python3 tools/fetch_mesa_switch.py` prints the prefix; second run is a no-op.
