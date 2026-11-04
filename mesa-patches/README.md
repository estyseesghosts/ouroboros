# mesa-patches — how to use

These patches are the original Switch-performance changes. They apply to the
`danfromtico/mesa-switch` fork at `main` (`0ff52079ca9`).

```bash
git clone https://github.com/danfromtico/mesa-switch.git
cd mesa-switch
git checkout 0ff52079ca9e9e837ed27eb95a8d239f439a1864
git am /path/to/ouroboros/mesa-patches/*.patch
```

Then build the Switch SDK prefix and link eidos against it per
`../docs/sdk-build.md` and `../eidos/switch/README.md`.

Do not commit Mesa build output, `.o`/`.a` files or vendored Mesa source
here — patches only. Full Mesa source stays upstream.
