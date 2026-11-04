# ouroboros — Switch port work monorepo

One repo for the Nintendo Switch homebrew work previously spread across
three checkouts plus local notes in `bw-switch/`.

- `eidos/` — snapshot of eidos at `switch/fix-h` (`9f12190`, 2026-11-04).
  Original MIT port code. No game data, no game source. Builds against a
  separate aletheia checkout (see `eidos-meta/aletheia-pin.md`).
- `mesa-patches/` — the 5 non-merge Mesa commits on top of upstream main,
  as `git format-patch` output, plus `PIN.md` with the exact commits.
  Apply onto a Mesa checkout; no full Mesa vendor in this repo.
- `eidos-meta/` — `aletheia-pin.md` (which decomp revision eidos builds
  against) and `BRANCHES.md` (every local eidos branch tip at snapshot time,
  so no work is lost).
- `docs/` — Switch plans, perf plans, Mesa feasibility notes. Original work.
- `docs/switch-mesa-checklists/` — A5 / A5b run checklists.
- `tools/` — local Switch run helpers (`session.sh`, `run.py`, `swmcp.py`).
- `logs-text/` — text-only run evidence (`stdout.txt`, `checklist.txt`,
  `env.txt`, `args.txt`, notes). All JPG/PNG screenshots excluded.

See `LEGAL.md`: this repo holds no ROMs, saves, extracts, keys, BIOS dumps,
NRO/ELF binaries or game screenshots.

## Provenance

- eidos upstream: https://github.com/Project-Holon/eidos
  - snapshot branch: `switch/fix-h`, commit `9f12190c7df4d75191b7c7082550ae32f5c2c7aa`
  - `switch/fix-h` is 81 commits ahead of local `perf/switch-integration`
    (`eefcb7f`), which itself merges upstream work plus Switch support.
  - full branch-tip table: `eidos-meta/BRANCHES.md`
- mesa upstream: local fork `danfromtico/mesa-switch`
  (Mesa with Switch nouveau/horizon support)
  - base: `main` at `0ff52079ca9`
  - tip: `perf/switch-integration` at `16732f61427`
  - our delta: 5 non-merge commits, 12 files, +187/-15 (see `mesa-patches/`)
- aletheia (decomp, NOT vendored): https://github.com/Project-Holon/aletheia
  - pinned: `e3c8c25280ee23515c2b44975ce5565aa86597db` (2026-11-03)
  - see `eidos-meta/aletheia-pin.md`

## Reproduce

```bash
# 1. eidos + aletheia side by side (aletheia provides game source at the pin)
git clone <this-repo> ouroboros
git clone https://github.com/Project-Holon/aletheia.git
cd aletheia && git checkout e3c8c25280ee23515c2b44975ce5565aa86597db && cd ..

# 2. Mesa with our patches
git clone <mesa-upstream> mesa-switch
cd mesa-switch && git checkout 0ff52079ca9
git am ../ouroboros/mesa-patches/*.patch
# then follow docs/sdk-build.md and eidos/switch/README.md

# 3. Build eidos (desktop check first)
cmake -S ouroboros/eidos -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/ilp32-toolchain.cmake \
  -DALETHEIA_ROOT=../aletheia -DEIDOS_BOOT=ON -DEIDOS_OVERLAYS=ALL
cmake --build build --target eidos_boot
ctest --test-dir build --output-on-failure
```

Switch NRO builds need devkitPro devkitA64 + libnx (see
`eidos/switch/README.md`, `eidos/switch/PORT-NOTES.md`,
`docs/sdk-build.md`). Title-mode only, run holding R from a title.

## What is deliberately excluded

`backups/*.sav` (Pokemon save files), `out/*.nro/*.elf` (696MB build
outputs), `logs/**/*.jpg` (74 game screenshots), `src/aletheia/extract/`
and `build/` (ROM-derived), all `build*/` dirs under worktrees (9.3GB),
`.DS_Store`, `__pycache__`. The `.gitignore` blocks all of these classes
(`*.nds`, `*.sav`, `*.nro`, `*.elf`, `*.key`, `extract/`, …).
