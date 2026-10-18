# Tony Hawk's SkateAndreas — install and play

Thanks for trying it. SkateAndreas lets you **skate around GTA San Andreas**: the whole city
becomes a skate park, with Tony Hawk's-style skating, grinds, manuals, BMX and a full soundtrack.

It works by running **reTHAWed** (the Tony Hawk's American Wasteland reimplementation) quietly
behind GTA. GTA draws the world and keeps its story; reTHAWed does the skating. You play in GTA's
window the whole time.

---

## 1. What you need

These don't come with the download — set them up yourself, in any order:

| You need | Why / notes |
|---|---|
| **GTA San Andreas, 1.0 US** | The "HoodLum" build. SkateAndreas checks the file and **won't start** on other versions (including the Steam 3.0 one). |
| **ASI Loader and Mod Loader** | Most GTA modding setups already have these. Without them GTA ignores `.asi` files, and the mod can't load. |
| **reTHAWed 5.0.0.1 or 5.0.0.2** | Its own folder, anywhere you like. SkateAndreas only *reads* from reTHAWed — nothing of ours is ever written inside it. |
| **GTA in a window or borderless** | **Not** exclusive full screen — in full screen, switching to reTHAWed freezes it. Press **Alt+Enter** in GTA to cycle the display mode, or install ThirteenAG's Windowed Mode plugin. |

**You do not need Python.** The download carries its own small Python for the map tool in step 3.

---

## 2. Install

Unzip the whole download into **its own folder** — `Documents\SkateAndreas` is a good place.
There is no installer; you just put the files where they belong:

| File | Goes |
|---|---|
| `SkateAndreas-launcher.exe` | Stays in the folder. This is the program you run. |
| `SkateAndreas-thaw.dll` | Stays **right next to** the launcher. |
| `tools\` (the map tool + its Python) | Stays **right next to** the launcher. |
| `INSTALL.md`, `README.md`, `LICENSE` | Leave them where they are. |
| `SkateAndreas.asi` | **Copy it** into `...\GTA San Andreas\modloader\SkateAndreas\` — make that folder yourself if it isn't there. |

Your folder should end up looking like this:

```
SkateAndreas\                       <- unzip here
   SkateAndreas-launcher.exe        <- run this
   SkateAndreas-thaw.dll
   INSTALL.md   README.md   LICENSE
   tools\
      gta_to_thaw_map.py
      python\                       <- a small Python just for the map tool
   extracted\                       <- created in step 3; leave it here afterwards
```

**Two rules that save most problems:**

- **Don't rename anything**, and keep the folder together — the launcher always loads the `.dll`
  beside it, and the map from step 3 has to stay beside it too.
- **`SkateAndreas.asi` must be inside the Mod Loader folder.** If it's anywhere else, GTA never
  loads it and nothing works.

---

## 3. Make the map (once)

This is the one fiddly bit, and you only do it once.

SkateAndreas skates on a reTHAWed map built **from your own GTA's collision data**, so it can't be
included in the download (that data is GTA's, not ours). You make it yourself in a few minutes.

1. Open `SkateAndreas-launcher.exe`.
2. Click **Build the map...** — it opens its own window and works for a few minutes, then opens a
   folder for you. *(It uses only the Python in `tools\python`. If it says that Python is missing or
   won't run, unzip the mod again. To use a Python you installed yourself instead, open
   `SkateAndreas.ini` next to the launcher and add `PythonExe=C:\full\path\to\python.exe` under
   `[Paths]`.)*
3. It has made an `extracted\` folder next to the launcher. **Copy the map folder inside it**
   (`extracted\thaw_maps\SkateAndreas San Andreas`) into reTHAWed's `data\mod\UserMods\Maps\`.
   *(The launcher can't do this step for you — nothing of ours writes inside your reTHAWed folder.)*
4. **Only one SkateAndreas map at a time.** If an older one is already there, remove it first.

**Updating from an older SkateAndreas?**

- **Keep your `extracted\` folder.** Unzip the new version over the old one (same folder), or
  unzip it somewhere new and copy the old `extracted\` folder next to the new launcher. The
  launcher looks for your map's files in `extracted\` next to itself. Without that folder it says
  the map is not built yet, even when the map in reTHAWed's `Maps` is fine, and the rails, natas
  posts and skitch cars won't work in game.
- **Rebuild the map only when the update notes say so.** Then replace the old map folder in
  reTHAWed's `Maps` with the new one. The button needs the reTHAWed folder set in the launcher
  (it reads the cars from your own reTHAWed; nothing is written there).
- If you don't have the old `extracted\` folder any more, just build the map again (step 2) and
  copy it over (step 3).

**Leave `extracted\` where it is.** The mod reads your rail list from it every time you play, so
if you move the launcher, move `extracted\` with it.

*(Prefer a command prompt? In the launcher's folder:
`tools\python\python.exe tools\gta_to_thaw_map.py "C:\Games\GTA San Andreas" --box -3000 -3000 3000 3000 --no-geometry-nodes --rail-pool 4500 --rail-drop-max 0 --name "San Andreas (SkateAndreas)" --folder "SkateAndreas San Andreas" --proxy-cars 8 --thaw-dir "C:\Games\reTHAWed"`
— same result as the button. Put your own GTA and reTHAWed folders in place of the two `C:\Games\...` ones.)*

---

## 4. Play

1. Double-click **`SkateAndreas-launcher.exe`**.
2. The window shows **five checks**. All green means you're good. A red line names exactly which
   file or folder is missing — usually `SkateAndreas.asi` (step 2) or the map (step 3).
   An **amber** line is only a warning; Play still works. If the `SkateAndreas.asi` line is amber
   and says GTA's copy is a different version, copy the `SkateAndreas.asi` from this download into
   GTA's `modloader\SkateAndreas\` again: the two games won't connect until they match.
3. If it guessed your GTA or reTHAWed folder wrong, click **Browse...** next to it and point at the
   right folder. It remembers your choice (in `SkateAndreas.ini`, beside the launcher).
4. Optional: tick **Start reTHAWed minimised (only GTA on screen)** — reTHAWed still runs, you just
   never see its window.
5. Click **Play**. reTHAWed starts first, then GTA opens on top of it. Leave the launcher's other
   messages alone until it says **Both games are running**.

**Free skate loads itself.** About 8 seconds after reTHAWed opens, it should drop into free skate
on your map by itself — you don't have to touch reTHAWed at all, and you'll see GTA's black message
box say **`Press L3+R3 to Skate`**.

*If you end up on reTHAWed's own menus instead:* alt-tab to reTHAWed, pick **Free Skate → your
SkateAndreas map → your skater**, wait until you're standing on the board, then alt-tab back to
GTA. You only need the map's own spawn once; after that the mod moves the skater to wherever CJ is.

When you close GTA, the launcher closes reTHAWed for you.

---

## 5. Controls

In GTA, once you're on foot:

- **L3 + R3** (click both sticks in together) — **start skating**. Press again to stop.
- On a **BMX** — **L3 + R3** turns **BMX mode** on and off instead (the game tells you the first
  time you get on a bike).
- **Cars** — the **roofs of parked cars** are grindable. To **skitch** a car, skate up behind it and
  use reTHAWed's own skitch (**Up** on the D-pad): CJ grabs the rear bumper and is pulled along,
  with reTHAWed's balance meter. Skitching starts working about a minute after the map has loaded
  (the mod first finds its stand-in cars inside reTHAWed); there is no key to press.
- **Radio** — hold **Back / Share** for the wheel: reTHAWed's soundtrack, any GTA station, the User
  Track Player, or Off. Move with the stick, D-pad Up/Down changes the volume.
- **Right stick** always moves the camera, in every mode.

ReTHAWed's own controls apply while a mode is on — its HUD (score, combo meter, balance meters) is
copied into GTA, so follow what the game shows you for grinds and tricks.

---

## 6. If something goes wrong

- **Read the launcher's red lines first** — they name the missing file or folder.
- **Logs** live in the `logs\` folder beside the launcher (`gta.log`, `thaw.log`, `launcher.log`;
  the previous run is kept as `.prev.log`). The **Open logs folder** button takes you there — send
  those files with any bug report.
- **Changed any of the files?** Restart both games (the launcher does reTHAWed for you; make sure
  GTA is closed).
- **Skate mode does nothing, or GTA says reTHAWed isn't in a level** — free skate didn't load; do
  it by hand (step 4).
- **CJ floats around instead of skating** — the wrong map is loaded, or more than one SkateAndreas
  map is installed in reTHAWed (step 3, point 4).
- **No rails at all** — the `extracted\` folder from step 3 was moved or deleted. It must stay next
  to the launcher. (`thaw.log` would say `rail pool: can't open ...`.)
- **GTA freezes when you switch to reTHAWed** — you're in exclusive full screen. Use a window
  (Alt+Enter).
- **No controller in reTHAWed** — it's set up for a PlayStation controller. A real Xbox controller
  may be ignored by reTHAWed for now.
- **A different reTHAWed version** — the launcher warns (amber) but still starts. If skating
  misbehaves, put 5.0.0.2 or 5.0.0.1 back, or send us the logs and we'll look.
- **Nothing else obvious** — check `README.md` for the current list of known limits.

---

## 7. What works right now

Skating across the whole of San Andreas; rails on ledges, roofs, bridges and the railroad tracks;
grinding parked cars and skitching moving ones; BMX mode; flips and bails; the radio wheel;
graffiti tags; the GPS route line; a locked 60 fps.

Not done yet: water (you ride along the sea bed), and a moving car can't physically block CJ.

`README.md` has the honest, full, up-to-date list.
