# mesa-switch pin

Our Switch Mesa work sits on top of the local fork `danfromtico/mesa-switch`
(which itself tracks upstream Mesa with Switch nouveau/horizon support).

- upstream fork remote: `https://github.com/danfromtico/mesa-switch.git`
- base: `main` at `0ff52079ca9e9e837ed27eb95a8d239f439a1864`
  (`horizon: runtime shutdown also closes the devices' cached buffers`)
- tip: `perf/switch-integration` at `16732f61427`
  (`Merge branch 'perf/submit-overhead' into perf/switch-integration`)
- delta `main..perf/switch-integration`: 10 commits, of which 5 are
  non-merge functional changes (in `*.patch` here) and 5 are branch merges.

## The 5 functional commits

1. `5d37f26e8e4` — nouveau/switch: give staging buffers CPU-cached memory
2. `4709a335fcf` — nvc0/switch: disable compressed kinds without comptag backing
3. `bf497bd027e` — nouveau/switch: stop clearing recycled BO backing on every allocation
4. `4b68dad9192` — nvc0/switch: suballocate texture upload staging and map it unsynchronized
5. `7816b59c93c` — nouveau/switch: fold the present CPU fence into the frame flush

Touched files (12): `src/egl/drivers/switch/egl_switch.c`,
`src/gallium/drivers/nouveau/{nouveau_buffer.c, nouveau_winsys.h,
nvc0/{nvc0_miptree.c, nvc0_screen.c, nvc0_screen.h, nvc0_transfer.c}}`,
`src/gallium/winsys/nouveau/switch/{nouveau_switch_libdrm.c,
nouveau_switch_public.h, nouveau_switch_pushbuf.c, nouveau_switch_winsys.c}`,
`src/nouveau/horizon/nouveau_horizon_memory.c` (+187/-15).

Verify with: `git diff --stat main..perf/switch-integration` in the fork.
