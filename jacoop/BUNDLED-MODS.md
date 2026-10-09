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
| HQ Icons | `GGHDIcons.pk3` (JKHub recommended list) | `gfx/hud/*`, `gfx/mp/*`, `gfx/menus/*`, `gfx/mplevels/*`, `gfx/mp/siegeicons/*` | High-res HUD, menu, ammo and force-power icons — including the `f_icon_*` set the team status panel draws |
| HQ chat icon | `(c)_HQ_Chat_Icon.pk3` (JKHub recommended list) | `gfx/2d/chat_icon_*.png`, `shaders/0chat.shader` | Crisper chat bubble/icon art |
| HQ sniper scope (Grab) | `GGScopeHQ.pk3` (JKHub recommended list) | `gfx/2d/cropcircle.tga`, `cropcircle2.tga`, `crop_charge.tga` | High-res Disruptor scope overlay (non-border variant; the bordered one ships in the same pk3 if ever wanted) |
| HQ fonts | `GGFontsHQ.pk3` (JKHub recommended list) | `gfx/2d/charsgrid_med.tga` only | The font `*.tga` atlases were deliberately **not** merged: the loader tries png before tga so they'd never render over our own Titillium fonts, and stock atlases + our `.fontdat` metrics would garble text. charsgrid_med is the legacy console/UI glyph grid — safe drop-in |

Credit to the original authors on JKHub — these are bundled as-is; if you're an
author and want yours removed, open an issue and it comes out.

## Optional, not in the installer

- **JKA Texture Overhaul 0.3** (~2 GB, 1073 files) — too big to bundle (it would
  take the installer from ~15 MB to ~2 GB and brush GitHub's per-asset cap).
  It's client-side anyway: drop `JKA_TexOverhaul.pk3` into `base/` on any
  machine that wants it. No conflict with `zz_jacoop.pk3`.

## Native equivalents (already in the engine — no pk3 needed)

| JKHub list item | How JACoop covers it |
|-----------------|----------------------|
| Enable dismemberment | `g_dismemberment` defaults to **3** (full — waist/head/limbs) and `g_dismemberProbabilities` to **0**, so any valid saber hit can sever — not just the rare flagged NPC. `g_saberMoreRealistic` defaults to **1** (precise collision + higher saber damage) and is a normal archived cvar, not cheat-gated. `g_corpseRemovalTime` defaults to **60**s so dismembered bodies linger. `g_saberPickuppableDroppedSabers` defaults **1** — dropped sabers are lootable |
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
