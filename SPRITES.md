# Pokémon sprite artwork

PKHeX-GC's box and party icons are the same sprites PKHeX draws, so a Pokémon
looks the same here as it does on the desktop.

- Source: `PKHeX.Drawing.PokeSprite/Resources/img/Big Pokemon Sprites/b_<dex>.png`
- Upstream repository: https://github.com/kwsch/PKHeX
- Upstream license: GNU GPL v3.0 (PKHeX itself)

`tools/build_sprites.py` downloads one 68×56 PNG per National Dex number 1-386
at build time, trims each to its visible pixels, scales it into a 32×32 cell,
and packs the result into a 1024×512 `GX_TF_RGB5A3` texture
(`data/gen3_sprites.bin`).

**No sprite image is stored in this repository.** The PNGs are cached under
`assets/cache/pkhex-sprites/`, which is git-ignored, and the generated atlas is
likewise not committed. A build with no network access falls back to a blank
atlas so the DOL still links; the UI simply draws no icons until it is rebuilt
with access.

The Pokémon artwork these sprites depict is © Nintendo / Creatures Inc. /
GAME FREAK Inc. PKHeX-GC claims no rights to it and redistributes none of it.

## Previously

Earlier revisions generated the atlas from the PokéSprite spritesheet project
(`msikma/pokesprite-spritesheet`, MIT for project code). That source is no
longer used.
