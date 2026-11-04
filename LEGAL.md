# Legal notice — ouroboros monorepo

This repository contains **no Nintendo or Pokemon game data**.

- No ROM images (`.nds`, `.3ds`, `.cia`), no extracted ROM contents, no
  converted assets, no disassembly listings, no save files (`.sav`), no
  game screenshots from test runs, no SDK dumps, no keys.
- `eidos/` is a snapshot of original port code (MIT, Project-Holon/eidos).
  At build time it compiles decompiled source from a separate aletheia
  checkout and reads data from the player's own legally obtained ROM.
  A release holds the built program and no game data.
- `mesa-patches/` holds original Mesa changes only, as patches against
  upstream Mesa. It does not vendor Mesa binaries or game data.
- `docs/`, `tools/`, `logs-text/` are original notes, helper scripts and
  text-only performance numbers. Screenshots (JPG/PNG) are excluded.

## What is licensed, and what is not

- `eidos/` (snapshot): MIT License (see `eidos/LICENSE`). Original code by
  its contributors.
- `mesa-patches/`: same license as Mesa for the changed files (MIT/X11);
  patches are original work.
- `docs/`, `tools/`, `logs-text/`: MIT (original work in this repo).

The reverse-engineered game source lives in aletheia
(https://github.com/Project-Holon/aletheia), pinned by commit hash only in
`eidos-meta/aletheia-pin.md`. That source is distributed without license
grant and remains the intellectual property of Nintendo, Creatures Inc. and
GAME FREAK inc. This monorepo does not redistribute it.

## Trademarks

Pokemon, Pokemon Black/White, Nintendo DS and Nintendo Switch are trademarks
of their respective owners. This is an unofficial fan project, not affiliated
with or endorsed by Nintendo, Creatures Inc., GAME FREAK inc. or The Pokemon
Company.
