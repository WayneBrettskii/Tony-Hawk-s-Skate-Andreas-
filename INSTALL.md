# SkateAndreas — how to install

SkateAndreas lets you **skate in GTA San Andreas** with the skating from **reTHAWed** (a Tony
Hawk's American Wasteland reimplementation). reTHAWed runs hidden behind GTA and does the
skating; GTA draws the world and keeps its story. Every ledge, rail, roof and bowl you can see
becomes skateable.

---

## 1. What you need first

None of these come with SkateAndreas — you install them yourself, in any order:

| You need | Notes |
|---|---|
| **GTA San Andreas, 1.0 US** | The "HoodLum" build. SkateAndreas checks the executable and **won't run on other versions** (including the Steam 3.0 one). |
| **ASI Loader + Mod Loader** | Most GTA modding setups already have these. Without them, `.asi` files are ignored. |
| **reTHAWed 5.0.0.1** | Its own folder, anywhere. We only *read* from it — nothing of ours is written inside it. |
| **Python 3** | Only for step 3, to build the map from your own copy of GTA. |

**Run GTA in a window or borderless — not exclusive full screen.** In exclusive full screen,
switching to reTHAWed freezes it. Alt+Enter in GTA cycles the display mode, or install
ThirteenAG's Windowed Mode plugin.

---

## 2. Where our three files go

Unzip the release. You get three files; where they go matters:

| File | Put it here |
|---|---|
| `SkateAndreas.asi` | `...\GTA San Andreas\modloader\SkateAndreas\` — make that folder |
| `SkateAndreas-thaw.dll` | **right next to** `SkateAndreas-launcher.exe` |
| `SkateAndreas-launcher.exe` | anywhere you like (a `SkateAndreas` folder in Documents is fine) |
| `tools\gta_to_thaw_map.py` | in a `tools` folder **next to** `SkateAndreas-launcher.exe` — it makes the map for step 3 |

Don't rename any of them. `SkateAndreas-launcher.exe` always loads the `.dll` beside it,
`SkateAndreas.asi` must be inside the Mod Loader folder or GTA won't load it, and the map tool
in step 3 writes its output beside the launcher — so keep those three together in the same
folder:

---

## 3. Build the map (once)

SkateAndreas skates on a reTHAWed map made **from your own GTA's collision**, so it can't be
shipped with the download — it is GTA's own data, converted. You make it once.

**The easy way:** press **Build the map...** in the launcher. It opens its own window and runs for
a few minutes, then opens the folder for you. It needs **Python 3** installed with *"Add Python to
PATH"* ticked at install time — the launcher tells you if it can't find it.

**The same thing by hand,** if you prefer a command prompt in the launcher's folder:

```
python tools\gta_to_thaw_map.py "C:\Games\GTA San Andreas" --box -3000 -3000 3000 3000 --no-geometry-nodes --rail-pool 4500 --rail-drop-max 0
```

Either way, it only reads your GTA, and afterwards:

1. Everything lands in an `extracted\` folder **next to the launcher**. **Leave that folder exactly
   where it is** — the mod reads the rail list from it every time you play. If you move the
   launcher, move `extracted\` with it.
2. Copy the map folder it made (`extracted\thaw_maps\<something>`) into reTHAWed's
   `data\mod\UserMods\Maps\`. (The launcher cannot do this for you — nothing of ours ever writes
   inside your reTHAWed folder.)
3. **Only one SkateAndreas map may be installed at a time.** If you already have one there,
   remove it first.

---

## 4. First run

1. Double-click **`SkateAndreas-launcher.exe`**.
2. It shows **four checks**. All green means you're ready. A red line tells you exactly what it
   couldn't find.
3. If it guessed your GTA or reTHAWed folder wrong, click **Browse...** next to it and point at
   the right folder — your choice is remembered for next time.
4. Click **Play**. reTHAWed starts first (you'll see it for a moment), then GTA starts on top.

---

## 5. Load free skate (one manual step)

SkateAndreas **cannot choose reTHAWed's game mode for you yet**, so do this once per session:

1. **Alt-tab to the reTHAWed window.**
2. As usual in reTHAWed: **Free Skate → your SkateAndreas map → your skater.**
3. Wait until you're standing on the board in the map, then **alt-tab back to GTA**.

(You need the map's own skatepark spawn only once; after that the mod moves reTHAWed's skater to
wherever CJ is.)

---

## 6. Skate

In GTA, press **L3 + R3** (click in both sticks at once). If it's your first time, GTA's black
message box will already have told you: **Press L3+R3 to Skate**.

- CJ is now driven by reTHAWed. Skate the streets, grind every ledge and rail, drop into bowls.
- **L3 + R3** again stops.
- On a **BMX**, **L3 + R3** toggles BMX mode instead (the mod tells you so the first time you get
  on a bike).
- While a mode is on, the controller belongs to reTHAWed — GTA ignores it. **The right stick
  still moves the camera.**
- Hold **Back / Share** for the radio wheel: reTHAWed's soundtrack, any GTA station, or off.

---

## 7. If something goes wrong

- **Read the launcher's red lines first** — they name the missing file or folder.
- **Logs:** `logs\gta.log`, `logs\thaw.log`, `logs\launcher.log` (the previous run is kept as
  `.prev.log`). The launcher's **Open logs folder** button opens them. Send those files with a
  bug report.
- **Changed our files? Restart.** GTA must be restarted for a new `.asi`; reTHAWed is restarted
  by the launcher (quit it completely first).
- **Skate mode does nothing, or says reTHAWed is not in a level** — you haven't done step 5 for
  this session.
- **CJ floats around instead of skating** — the map isn't the one the mod expects, or more than
  one map is installed (step 3.5).
- **GTA freezes when you switch to reTHAWed** — you're in exclusive full screen; use a window.
- **No rails at all** — the `extracted\` folder from step 3 has been moved or deleted; it must
  stay next to the launcher (`logs\thaw.log` would say `rail pool: can't open ...`).
- **No controller in reTHAWed** — the launcher currently tells reTHAWed to ignore Xbox-style
  controllers (it's set up for a PlayStation controller plus DualSenseX). A real Xbox controller
  will be ignored for now; this becomes an option before release (`docs/DESIGN.md`, section 11).

---

## 8. What works, what doesn't

See **`README.md`** for the honest, up-to-date list. The short version: skating across the whole
map, rails everywhere (ledges, roofs, bridges, railroad tracks), grinding parked and slow cars,
skitching a moving car, BMX mode, the radio wheel, graffiti tags and the GPS line all work.
Loading free skate automatically on Play does **not** (that's step 5), and a moving car can't
physically block CJ.
