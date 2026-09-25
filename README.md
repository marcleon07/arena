# Arena

A classic arena deathmatch shooter (Quake 3 style) in Unreal Engine 5 C++, with
**Half-Life 1 / GoldSrc movement**: air strafing, bunnyhopping, crouch-jumping,
rocket jumping and jump pads. Up to 16 players over listen or dedicated servers.

The map, lighting, player models, weapons and effects are all generated in code
from the engine's built-in shapes. The only content assets are the sound effects,
which are synthesized by a script (see [Sounds](#sounds)).

## Requirements

- Unreal Engine 5.4 or newer (from the Epic Games Launcher)
- Visual Studio 2022 with the "Game development with C++" workload

## Build

1. Right-click `Arena.uproject` → **Switch Unreal Engine version…** → pick your install.
2. Right-click `Arena.uproject` → **Generate Visual Studio project files**.
3. Open `Arena.sln`, set configuration to **Development Editor**, build.
4. Open `Arena.uproject`. In the Play dropdown set *Number of Players* to 2+ and
   *Net Mode* to **Play As Listen Server** to test multiplayer in-editor.

## Menus

Launching the game normally (double-click a packaged build, or **Play → Standalone
Game** in the editor) opens the **main menu** over a slow camera orbit of the arena:

- **Host Game**: pick a map, a frag limit and a time limit, then start a listen server that friends can join.
- **Join Game**: enter an IP (or `IP:port`). The last address is remembered.
- **Settings** has two tabs:
  - **General**: player name, mouse sensitivity, invert mouse, field of view, volume,
    auto-hop, fullscreen, VSync, frame-rate limit and an FPS counter.
  - **Controls**: every action has two key slots. Click one, then press a key, a
    mouse button or scroll the wheel. Esc cancels, Backspace clears the slot, and a
    key you reuse is taken off whatever it was bound to before. **Reset to defaults**
    restores the keys below.

  Everything is saved to `Saved/Config/Windows/GameUserSettings.ini`.

In a match, **Esc** (or **F10**) opens the pause menu: Resume, Settings, Leave Match and
Quit. The match keeps running while it's open. In Play-In-Editor, Esc stops the
session, so use F10 there.

Playing in the editor as **Listen Server**, or running `Host.bat` / `Join.bat`, skips
the main menu and drops you straight into a match.

## Maps and voting

| Map | Layout |
| --- | --- |
| **Courtyard** | Walled square: corner towers joined by a bridge ring, a central platform with ramps, jump pads up to the towers |
| **Skyline** | Floating islands over a void (Longest Yard style). Jump pads out to the side islands, bhop gaps to the lower corners, pads up to a high Mega platform. Falling off kills you |
| **Canyon** | Long walled strip with raised bases at both ends and a sloped valley in the middle to ramp through for speed, or cross on the bridge |

When a match ends (frag or time limit), the scoreboard stays up and everyone gets
15 seconds to vote for the next map by clicking a card. The most votes wins, and ties
(or no votes) are decided at random. Then the server moves everyone to the new map
with the same limits. `VoteDuration` in `Config/DefaultGame.ini` changes the timer.

Maps are defined in code in `ArenaMap.cpp` (`FArenaMapDef`: geometry, spawns, items,
kill height, menu camera). Every map loads on the same engine level; the server picks
one with the `Arena` URL option and replicates the choice.

## Play outside the editor

The scripts in `Scripts/` find the engine under `C:\Program Files\Epic Games\UE_5.*`,
or you can set `UE_ROOT` yourself.

| Script | What it does |
| --- | --- |
| `Host.bat` | Starts a listen server (you play and host) |
| `Join.bat [ip]` | Joins a server (defaults to `127.0.0.1`) |
| `DedicatedServer.bat` | Headless server on port 7777 |
| `ImportSounds.bat` | Regenerates and re-imports the sound effects |

Match options go on the URL: `Host.bat` passes extra args through, or from the
console: `open /Engine/Maps/Entry?listen?Arena=Skyline?FragLimit=30?TimeLimit=15`.

## Controls

Defaults; all of them except the menu and console keys can be changed in
**Settings → Controls**.

| Key | Action |
| --- | --- |
| WASD / arrows | Move |
| Space | Jump (hold to auto-hop) |
| Mouse wheel down | Jump (HL1-style scroll bhop) |
| Ctrl / C | Crouch |
| Left mouse | Fire, or respawn when dead |
| 1 – 8 | Gauntlet, Machinegun, Shotgun, Grenade Launcher, Rocket Launcher, Lightning Gun, Railgun, Plasma Gun |
| E / mouse wheel up | Next weapon (skips empty ones) |
| Q | Last weapon |
| Tab | Scoreboard |
| Esc / F10 | Menu |
| ` | Console |

### Console commands

| Command | Effect |
| --- | --- |
| `autohop 0` | Turn off hold-to-hop, so you have to time jumps or scroll like in HL1 |
| `sens 2.5` | Quake-style sensitivity (0.022° per mouse count × value) |
| `fov 110` | Field of view |
| `airaccel 100` | Sets the server's `sv_airaccelerate` (10 = HL1, 100 = CS 1.6 surf/bhop servers) |
| `setname Frag` | Change your name for this match (set it permanently in Settings) |

## How the movement works

`UArenaMovementComponent` replaces Unreal's velocity model with the one from
GoldSrc's `pm_shared.c`, converted to centimetres (1 unit = 2.54 cm):

- **Ground:** `PM_Friction` (friction 4, stopspeed 100) + `PM_Accelerate` (accelerate 10), max speed 320.
- **Air:** `PM_AirAccelerate` caps only the *projection* of velocity onto your wish direction at 30 u/s.
  Holding A while turning the mouse left keeps that projection near zero, so each tick adds speed.
  That is air strafing.
- **Bunnyhop:** friction is skipped on any ground tick where a jump is queued, so hopping as you
  land keeps all your speed. HL1's later `PM_PreventMegaBunnyJumping` cap is available but off
  (`bCapBunnyhopSpeed` in `Config/DefaultGame.ini`).
- Gravity 800, 45-unit jump, 18-unit steps, walkable slopes up to normal.z 0.7, and a
  72/36-unit standing/ducked hull. Crouch-jumping works.
- Unreal's air control and horizontal terminal-velocity clamp are bypassed, so speed has no ceiling.

All of this depends only on velocity, input acceleration and the jump flag, and those
are already part of Unreal's networked saved moves. Clients predict it exactly and the
server replays the same math.

The HUD speedometer under the crosshair shows horizontal speed in units/sec. It turns
green once you pass running speed.

## Weapons and items (Quake 3 values)

| Key | Weapon | Damage | Refire | Notes |
| --- | --- | --- | --- | --- |
| 1 | Gauntlet | 50 | 0.4 s | Melee (64 units), no ammo, always carried |
| 2 | Machinegun | 7 | 0.1 s | Hitscan, slight spread, start weapon |
| 3 | Shotgun | 11 × 10 | 1.0 s | Hitscan pellets; hits are summed into one hit per player |
| 4 | Grenade Launcher | 100 + 100 splash | 0.8 s | 700 u/s lob, bounces, 2.5 s fuse, explodes on touching a player |
| 5 | Rocket Launcher | 100 + 100 splash | 0.8 s | 900 u/s, 120-unit splash; full self-knockback at half damage, so rocket jumps work |
| 6 | Lightning Gun | 8 | 0.05 s | Continuous beam, 768-unit range |
| 7 | Railgun | 100 | 1.5 s | Hitscan, passes through up to 4 players |
| 8 | Plasma Gun | 20 + 15 splash | 0.1 s | 2000 u/s bolts with a 20-unit splash, enough to plasma-climb walls |

Spread weapons use a random seed that the shooter sends with each shot, so the tracers
you see locally match the server's pellets. Weapons respawn 5 s after pickup.

Health (+25), Mega Health (+100, max 200), Armor (+50), Heavy Armor (+100), and
ammo boxes all respawn on Quake 3 timers. Armor absorbs 66% of damage. Health and
armor above 100 count down one point per second.

## Combat feedback

- **Hit sound** on every hit you land, pitched by damage (Quake 3 style), plus a hit
  marker on the crosshair (red on the killing blow) and a floating damage number.
- **Taking damage** flashes the screen red and shows an arc around the crosshair
  pointing toward the shooter or explosion.
- **Frag messages** in the center: "You fragged X / 2nd place with 7", or "Fragged by X".
- Weapon, explosion, pain, death, footstep (every 0.3 s when running, silent when
  ducked), jump, landing, jump pad, pickup and spawn sounds, all positional.

## Sounds

`Tools/generate_sounds.py` synthesizes every effect (pure Python, no dependencies)
into `SourceAudio/*.wav`, and `Tools/import_sounds.py` imports them into
`Content/Audio` through the editor. `Scripts/ImportSounds.bat` runs both. Close the
editor first.

To use a real sound instead, drop a WAV with the same name (e.g. `RailFire.wav`)
into `SourceAudio/`. Then run only the import step, or import it in the editor over
the existing asset. The game loads sounds by name from `/Game/Audio`, and a missing
sound is skipped with a log warning.

## Code map

| File | Role |
| --- | --- |
| `ArenaMovementComponent` | GoldSrc movement (the core of the bhop feel) |
| `ArenaTypes` | Weapon table (damage, refire, ammo, spread, range), sounds, pickups |
| `ArenaCharacter` | Input, firing, damage/armor, knockback, death |
| `ArenaPlayerController` | Input actions and key bindings, menus, settings, respawn requests |
| `ArenaGameMode` | FFA deathmatch rules, spawn selection, frag/time limits |
| `ArenaGameState` | Replicated match state, `sv_airaccelerate`, effect multicasts, kill feed |
| `ArenaMap` | The map registry: each map's geometry, jump pads, spawns and items, plus lighting |
| `SArenaMapVote` | End-of-match map vote cards |
| `ArenaProjectile` | Rocket, grenade and plasma projectiles (direct + splash damage) |
| `ArenaPickup` | Health, armor, weapon and ammo items |
| `ArenaHUD` | Canvas HUD, scoreboard, hit markers, damage numbers and indicators |
| `ArenaAudio` | Loads `/Game/Audio` and plays 2D and positional sounds |
| `SArenaMenu` | Slate main and pause menu (no widget assets) |
| `ArenaSettings` | Per-user preferences and key bindings saved to `GameUserSettings.ini` |

## Known limitations

- Hitscan has no lag compensation, so at high ping you have to lead your shots.
- Visuals are placeholders built from engine shapes, and the sounds are synthesized placeholders.
- The level is built at runtime on top of the engine's empty `Entry` map. If your
  engine version shows something odd there, create an empty level
  (File → New Level → Empty Level), save it as `/Game/Maps/Arena`, and point the
  map settings in `Config/DefaultEngine.ini` at it.
