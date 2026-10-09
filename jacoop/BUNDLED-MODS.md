# Bundled QoL Mods & Enhancements

JACoop ships a few community QoL mods merged into `zz_jacoop.pk3`, plus native
code equivalents for other items on the JKHub recommended list
(https://jkhub.org/mods/jediacademy/). All bundled files live under
`jacoop/pk3/` — delete the listed paths there and repack to remove a mod.

## Bundled asset mods (in zz_jacoop.pk3)

| Mod | Source | Files | What it does |
|-----|--------|-------|--------------|
| Better Force lightning effects | `z_zzDLightningPL.pk3` (JKHub recommended list) | `effects/force/lightning.efx`, `effects/force/lightningwide.efx`, `gfx/misc/blueline.jpg`, `gfx/misc/lightning3.jpg`, `gfx/misc/lightningflash.jpg` | Replaces the stock lightning effect with a brighter, flickering blue bolt |
| Water Wake Fix | `0Wake.pk3` (JKHub recommended list) | `shaders/awake.shader` | Fixes the broken water-splash wake animation around players wading through water |

Credit to the original authors on JKHub — these are bundled as-is; if you're an
author and want yours removed, open an issue and it comes out.

## Native equivalents (already in the engine — no pk3 needed)

| JKHub list item | How JACoop covers it |
|-----------------|----------------------|
| Enable dismemberment | `g_dismemberment` defaults to **3** (full — waist/head/limbs). `g_saberMoreRealistic` is a normal archived cvar here, not cheat-gated |
| Custom resolution | OpenJK `r_customWidth`/`r_customHeight` + `r_mode -2`; UI scales automatically |
| High FOV fix | `cg_fovAspectAdjust 1` (default) — Hor+ scaling on wide screens |
| Simple dot crosshair | `cg_crosshairDot 1` |
| HQ chat/console | JACoop ships its own higher-res fonts (`fonts/` in the pk3) and co-op HUD art |
| Dynamic light for weapons | `r_dynamiclight 1` (renderer default) |
| Ingame menus / npcSP | `F6` co-op menu (lobby, player/skin, force points, rescue teleport); `npc spawn <type>` console command |

## Deliberately not bundled

- **Model/skin packs** (Jaden Remastered, ROTJ troopers, hilt packs…) — large,
  opinionated, and player-choice territory. They install cleanly on top:
  drop the pk3 into `base/` after install.
- **Overhauls** (Galactic Legacy, Movie Duels, Movie Battles…) — total
  conversions that would replace JACoop rather than complement it.
- **Other clients** (JAEnhanced, TaystJK…) — JACoop *is* an OpenJK-based
  client/engine fork; they aren't compatible.

## Regenerating the pk3

CI packs it with `Compress-Archive`, but on Windows PowerShell 5.1 that writes
backslash path separators that the game can't see. Use Python:

```bash
python -c "import zipfile, os; z = zipfile.ZipFile('build/zz_jacoop.pk3','w',zipfile.ZIP_DEFLATED); [z.write(os.path.join(r,f), os.path.relpath(os.path.join(r,f),'jacoop/pk3').replace(os.sep,'/')) for r,_,fs in os.walk('jacoop/pk3') for f in fs]; z.close()"
```
