# Quest 3 Strikers

<p align="center"><img src="media/quest3-strikers.jpg" width="360" alt="Quest 3 Strikers: a mixed reality soccer experience"></p>

Super Mario Strikers (GameCube), running natively on a Meta Quest 3 as a mixed-reality app. Not an
emulator: the game is built from the community decompilation, compiled for the headset's ARM chip,
and drawn straight into your room through OpenXR.

This is a fork of [`luisxl15/Strikers-Android-Port`](https://github.com/luisxl15/Strikers-Android-Port),
the Android phone port, with a Quest layer on top.

*You need your own copy of the game. **No game files are included**, in this repository or in the
APK.*

### [⬇ Download the latest version](https://github.com/LanceSweatpants/Strikers-Quest-MR/releases/latest)

On that page, under **Assets**, get the file ending in **`.apk`** (not "Source code"). Then follow
[How to install](#how-to-install-easy-version).

## What it is

Four ways to watch the same match, switched any time by clicking the controller sticks (see
[Controls](#controls-quest-3-controllers)):

- **Jumbotron · Living Room**: a big flat screen floating in your real room (passthrough), in three
  sizes. This is where the game starts.
- **Jumbotron · Stadium**: the same screen, but you stand on the pitch of a life-size stadium, the
  one your match is being played in.
- **Tabletop · Living Room**: the match as a miniature 3D pitch in your room, like a foosball table
  with the electric fence for walls. Put it on a real table, the bed or the floor, walk around it,
  grab it, turn it and resize it with your hands. The players are drawn a bit bigger so they read at
  table size, an arrow floats over the player you control, and a small score board hangs over the
  field.
- **Tabletop · Stadium**: the table inside the life-size stadium, with the stands all the way around
  you.

And around the game:

- **Two versions, Classic and Plus** (alpha 1.3 and newer): pick one with **Version** on the app's
  first screen. **Classic** is the game as alpha 1.2 had it. **Plus** adds things that aren't in the
  original game:
  - **A life-size goal scorer**: whoever scores runs out into your room (or onto the stadium pitch),
    stops out in front of you (in Tabletop, right beside the table), and waves and dances while the
    replay screen rises above them.
  - **A "GOAL SCORED BY" banner** over their head with their goals and assists in this game.
  - **Hat tricks**: three regular goals by one player in one game and hats rain down all over the
    field. (A Super Strike worth two counts towards the stats but not towards a hat trick.)
  - **A longer goal replay** that keeps playing through the celebration.
  - **A living crowd**: louder in close games, quieter in blowouts, a roar on every goal, and in the
    stadium the crowd sounds come from where the stands are.
- **Goal celebrations in 3D**: confetti in the scorer's colours plus their own piece (coins for
  Mario, hearts for Peach, bananas for Donkey Kong, and so on), streamers, foil, pyro and fireworks,
  raining down from the sky and fading where they land (no heaps). In Tabletop the goal replay plays
  on the screen over the table (in the stadium, on a jumbotron that drops from the sky), and the
  scorer celebrates standing right beside the table. Skip the replay and it all stops.
- **The game's own white flashes** (replay wipes, the Super Strike flash) light up your whole view,
  not just the picture.
- **Controller rumble**: the game's rumble reaches the Touch controllers, with an extra buzz on your
  goals.
- **Online 1v1**: **Quick match** pairs you with anyone else searching, or one player hosts and reads
  out a four-letter code and the other types it in. Each player keeps their own view mode.
- **Tournaments** (alpha 1.5): 4- or 8-player knockouts with your own team name and star player as
  your logo, a playoff tree, and results that report themselves. They start when they're full.
- **Voice chat** in online matches (alpha 1.4): turn it on with **Voice chat** on the app's first
  screen, or in the game's pause settings.
- **Pause settings**: crowd volume, voice chat, and your eye height in the stadium (for playing seated,
  or if you're shorter or taller).
- **Button mapping** (alpha 1.6): put pass, shoot, items, hit, swap items, L, R, pause and (alpha 1.8)
  the long pass on whichever buttons you like, with **Controls** on the app's first screen.
- **Bluetooth controllers** (alpha 1.8): a paired gamepad is your player too, right alongside the Quest
  controllers, offline and online, and follows your button map.
- **Responsive sticks** (alpha 1.9): your player moves from the first bit of push and goes exactly the
  way you point, diagonals included, at any amount of push (the original game snapped part pushes
  onto straight lines and ignored light ones).
- **RetroAchievements** (alpha 1.6): log in with your [RetroAchievements](https://retroachievements.org)
  account and earn the site's official Super Mario Strikers achievements as you play, with a pop-up in
  the headset for each one.
- **Your disc image as it is**: `.iso`, or Dolphin's `.rvz` and `.wia` with any compression. Pick
  it with **Choose game file**, which also tells you if a file isn't the right game.
- **Updates from inside the app**: when there's a new version, the first screen offers **Update
  now**.

## Status

**Free alpha.** Tested on Quest 3, including online matches between two Quest 3 headsets. Every
other headset is untested: the Quest 3S has the same chip and should behave the same, but nobody
has tried it yet. Expect rough edges, and see [Known issues](#known-issues).

## How to install (easy version)

> **Already have it?** On alpha 1.2 or newer: open the app and press **Update now**.
> On alpha 1 or 1.1: just do **Step 3** below, once.

**You need:**
- A **Meta Quest 3** and a **USB-C cable**
- A **PC**
- **Your own** Super Mario Strikers (GameCube) game file: **`.iso`**, **`.rvz`** or **`.wia`**

### Step 1 · Developer Mode *(only once)*
1. On your phone, open the **Meta Horizon** app.
2. **Devices** → your Quest → **Headset settings** → **Developer mode** → **On**.
   *(Asked to make a free developer account? Do it, then come back.)*
3. Restart the Quest.

### Step 2 · SideQuest *(only once)*
1. On your PC, install the free [**SideQuest Advanced Installer**](https://sidequestvr.com/setup-howto) and open it.
2. Plug the Quest into the PC.
3. Put the Quest on. At **"Allow USB debugging?"** tick **Always allow** and press **Allow**.
4. The dot at the top left of SideQuest turns **green**. You're connected.

### Step 3 · The app
1. [**Download the `.apk`**](../../releases/latest): under **Assets**, the file ending in `.apk`.
2. Drag it onto the SideQuest window. Wait until it says it's done.

### Step 4 · Your game file
1. In SideQuest, click the **folder icon** at the top.
2. Open **Download**.
3. Drag your `.iso`, `.rvz` or `.wia` in. *(Zipped? Unzip it first.)*

### Step 5 · Play
1. Unplug the Quest and put it on.
2. **Library** → **All** (top right) → **Unknown Sources** → **Quest 3 Strikers**.
3. Press **Play offline**.
4. **First time only:** turn on **"Allow access to manage all files"**, go back, and press
   **Play offline** again.

- **Game not found?** Press **Choose game file** and tap yours.
- **Next time?** Just Step 5.
- **New version out?** The app shows **Update now**. Press it, then **Install**. *(The first time,
  allow the app to install updates.)* Your saves and game file stay.

Now see [Controls](#controls-quest-3-controllers) to switch to the tabletop and the stadium.

## Controls (Quest 3 controllers)

### Playing

| Button | What it does |
| --- | --- |
| **Left stick** | Move |
| **A** | Pass |
| **B** | Shoot |
| **X** | Use your item |
| **Y** | Hit |
| **Right grip** (squeeze) | Swap items |
| **Left / right trigger** | The GameCube's L / R |
| **Right stick** | The GameCube's C-stick |
| **Menu button** (left controller, ☰) | Pause |

Want them somewhere else? See [Button mapping](#button-mapping).

### Switching views

"Click" a stick means **press it straight down** like a button. Switching needs **two quick
clicks**, so you can't do it by accident mid-match.

| Do this | What happens |
| --- | --- |
| **Left stick: click twice fast** | Swap between **your room** (mixed reality, you see your real room) and **the stadium** (full VR, you're standing on the pitch) |
| **Right stick: click twice fast** | Swap between the **big screen** and the **tabletop** (a mini 3D pitch you can walk around) |
| **Right stick: click once** | Big screen: change its size (3 sizes) |
| **Right stick, while paused** | The settings panel: **up/down** picks crowd volume, voice chat or stadium height, **left/right** changes it |

So there are four views, and you can mix them any way you like:

| | Big screen | Tabletop |
| --- | --- | --- |
| **Your room** (MR) | Where the game starts: a giant screen floating in your room | The mini pitch on your real table, bed or floor |
| **Stadium** (VR) | The screen, with you standing in the stadium | The mini pitch, with the whole stadium around you |

### Moving the table (Tabletop)

- **Grab it:** hold the **left grip** and move your hand. The table follows, up and down too. Let
  go to drop it there.
- **Make it bigger or smaller, or turn it:** keep holding the left grip and push the **left
  stick** up/down to resize it and left/right to turn it. (While you hold the left grip, your
  player stands still: the stick is moving the table.)
- **With both hands:** hold the left grip, put your right hand on the table and squeeze the
  **right grip** too. Pull your hands apart or together to resize it, and twist them to turn it.
- **Right hand only:** squeeze the right grip with your hand on the table to grab it. Anywhere
  else, the right grip swaps items as usual.
- Lost the table? Hold the **left grip** anywhere: it grabs the table wherever it is, so bring it over
  and let go. (Since alpha 1.10 a single click of the left stick does nothing, so a hard push on the
  stick mid-match can't make the table jump.)

### Moving the big screen (Jumbotron)

The same grips work on the big screen: hold the **left grip** to move it, hold **both grips** and
pull apart or twist to resize and turn it. Where you put it is remembered.

A Bluetooth gamepad paired to the headset works too (alpha 1.8): it's the same player as your Quest
controllers, so use either, offline or online. Its buttons follow the [Button mapping](#button-mapping)
(A/B/X/Y as they're labelled, LT/RT for the triggers, RB for the right grip, Start for the menu
button).

## Play online with a friend

Both of you need the **same release** of the app, set to the **same version** (both Classic or both
Plus), and your own game files.

1. **One of you hosts:** press **Host an online match**. You get a fresh 4-letter code. Tell it to
   your friend, then press **Start**.
2. **The other joins:** press **Join with a code**, type the code, press **Join**.
3. **Both of you:** pick **Grudge Match** on the main menu, choose your teams and start. Whoever
   gets there first waits on the loading screen, with a card showing the room code, for as long as
   it takes the other one to join and start too (alpha 1.8). Then you're playing each other. Tired
   of waiting? Hold **B** for 2 seconds to play the computer instead.

**No friend online? Quick match** (alpha 1.4): press **Quick match** and wait. As soon as someone
else on the same release and Version is searching, you're both put in the same room. Then both of
you: **Grudge Match**, pick your team, start.

**Voice chat** (alpha 1.4): press **Voice chat** on the app's first screen to turn it on (the first
time, allow the microphone). In an online match you hear each other; it's off until you turn it on,
and you can turn it off again in the game's pause settings (right stick down to **Voice chat**, then
left). If only one of you has it on, neither hears the other.

## Tournaments

Alpha 1.5. Knockout tournaments for 4 or 8 players, on the same release and Version as you.

1. Press **Tournaments** on the app's first screen.
2. Type your **team name** and tap a **star player**: it's your team's logo in the lists and the tree.
3. Tap the **4-player** or **8-player tournament**. The page shows how full each one is and who's in.
   It starts by itself as soon as it's full, so stay on this page.
4. Your match counts down from 10 and starts (or press **Play your match**). It's a normal Grudge
   Match: both of you pick your team and start.
5. When the match ends you come back to the **playoff tree**. The result reports itself; the
   winner goes through to the next round, until there's a champion.

- A draw, or two headsets that report different results, means the match is played again.
- Leaving the tournament mid-match counts as a loss. If your opponent never turns up, after 5
  minutes you can claim the win.
- A tournament nobody plays on for an hour is dropped.

## Button mapping

Alpha 1.6. Press **Controls** on the app's first screen. Tap an action (Pass, Shoot, Use your item,
Hit, Swap items, GameCube L, GameCube R, Pause, and from alpha 1.8 **Long pass**), then the Quest
button you want for it: **A, B, X, Y, left or right trigger, right grip** or the **menu button** (or
**Nothing**). A button that was already in use swaps over to the action you took it from. **Back to
the default buttons** puts everything back. Changes apply even with the game already running. The
sticks, the stick clicks and the left grip (which moves the table) stay where they are.

- **Long pass** does the game's L + A (the lob pass) from one button. It's on nothing until you
  give it a button.
- A **Bluetooth gamepad** follows the same map: its A/B/X/Y, LT/RT, RB (as the right grip) and
  Start (as the menu button).

## RetroAchievements

Alpha 1.6. Earn [RetroAchievements](https://retroachievements.org)' official Super Mario Strikers
set (40 achievements) as you play.

1. Make a free account on [retroachievements.org](https://retroachievements.org) if you don't have one.
2. Press **RetroAchievements** on the app's first screen and log in. Only a login token is kept on
   your Quest, never your password. The page lists the whole set and what you've unlocked.
3. Start the game. A pop-up shows how many you have, and each new one pops up as you earn it and
   goes to your RetroAchievements profile.

- Achievements count in **casual** mode. RetroAchievements only allows hardcore on apps it has
  approved, so the page and your profile show a warning about that; casual unlocks still count.
- RetroAchievements supports one copy of the game: **Super Mario Strikers (USA)**, the standard
  (Redump) dump, as `.iso`, `.rvz` or `.wia`. NKit files of it work too (alpha 1.7). Other regions
  (Mario Smash Football) and other dumps aren't on RetroAchievements. If yours isn't recognised, the
  pop-up and the app's RetroAchievements page say why and show your file's ID.
- Logged in while the game was already running? Close the game and start it again.

## If something goes wrong

- **SideQuest says "there's no apk in that file":** you downloaded "Source code". On the
  [Releases page](../../releases/latest), under **Assets**, download the file ending in **`.apk`**.
- **"Your game file wasn't found" / the app closes when you press Play** (alpha 1.1 and older):
  press **Choose game file**. It lists every `.iso`, `.rvz` and `.wia` on the headset and says what's
  wrong with any it can't use. If the list is empty, your game file isn't on the headset yet, or
  it's still inside a `.zip`/`.7z`: unzip it on your PC and do Step 4 again.
- **The menu keeps asking for file access:** in the settings screen, make sure the switch is on for
  **Quest 3 Strikers**.
- **The game closed:** open it again from Unknown Sources. If the game stopped because of a problem,
  the first screen shows its message.
- **Online, you ended up playing the computer:** back out to the main menu. Make sure you both
  used the same code, then both start Grudge Match again.
- **Still stuck?** [Report a bug](../../issues/new?template=bug_report.yml).

<details>
<summary>More detail: disc versions, file formats, where the game looks</summary>

- The US version (disc code `G4QE01`) is the tested one. The port also reads the European
  (`G4QP01`, sold as *Mario Smash Football*) and Japanese (`G4QJ01`) discs; those are untested on
  the headset.
- Formats: `.iso` (NKit too), and Dolphin's `.rvz` (zstd, bzip2, LZMA, LZMA2 or none) and `.wia`
  (purge, bzip2, LZMA, LZMA2 or none). Older `.gcm`, `.ciso` and `.gcz` files are still read if the
  game finds them by itself, but **Choose game file** lists only `.iso`, `.rvz` and `.wia`.
- Where the game looks for the disc: the file picked with **Choose game file**. With none picked, in
  order: a folder called `Strikers` at the top of the headset's storage (the game creates it, and
  keeps its settings and log there), then the top of the storage itself, `Games/Strikers`,
  `ROMs/GameCube` and `Download`. If a folder holds several GameCube disc images, the game takes the
  one whose name comes first alphabetically.
- The first screen asks GitHub once each time it opens whether there's a newer release (nothing
  else is sent). Offline, it just doesn't show **Update now**.
- Instead of SideQuest you can use `adb install` and `adb push`, or the headset's own USB file
  access (allow it inside the headset, and it shows up on the PC as a drive).
- Hand tracking is not supported; use the Touch controllers.

</details>

More about online play:

- Start your Grudge Match only once both of you are in the game with the code. If your friend is
  not in the room yet, your match starts offline straight away; back out and start again. If they
  never start one, yours goes offline after three minutes.
- One headset is picked automatically as the home side, and its stadium and match settings are
  used.
- Each player has their own view: one can play on the table while the other plays on the big
  screen.
- If the connection is lost for about ten seconds, the online match ends. Go back to the menu and
  start a new Grudge Match.
- A room code is not a password: anyone who types the same code lands in the same room. While two
  players are playing, a third headset in the room is ignored. Join also takes your own made-up
  code (3 to 8 letters or numbers), as long as you both type the same one.
- The connection goes through a small free relay (see [The room server](#the-room-server)). It only
  passes game messages along (button presses, team picks, and with voice chat on, your voice) and
  stores nothing. Like any web service, its host sees your IP address.
- How far behind your presses land online depends on the connection to that relay: the game measures
  it before the match and sets the delay for the whole match (usually about a tenth of a second).


## Known issues

- **Jumbotron · Stadium runs a little under 60 frames a second** (about 56 to 58), and Tabletop ·
  Stadium can dip during goal replays and celebrations. The other modes run at a full 60. The game
  itself always runs at its normal speed.
- **Online, the slower headset sets the pace for both.** A headset that keeps falling behind in a 3D
  mode redraws its 3D view every other frame until you change view.
- **Both players need the same release of the app, and the same version (Classic or Plus).** With
  different ones the two headsets ignore each other and each plays offline. Players on different disc
  regions are untested.
- **Quest 2:** its passthrough is grey, so in Tabletop the Super Strike shows on a screen beside the
  table. On a Quest 3 you watch it happen on the table itself.
- **The game's video clips don't play** (the intro and attract movies): this build has no video
  decoder. Everything else runs without them.
- **Don't open the app from the Library during a match.** The panel comes up in front of the game
  and pauses it; online, your friend's headset ends the match after about ten seconds. Use the
  Quest menu to quit, then open the app again.
- **The shared room server runs on Cloudflare's free plan.** If it hits the free daily limit,
  online play won't connect until the limit resets (00:00 UTC). Offline play is not affected.

To report a problem, the game's log is `Strikers/strikers-log.txt` on the headset's storage.

## Building from source

Most players don't need this. The build is two steps: CMake and the Android NDK build the game,
then Gradle packages it.

You need:

- The Android SDK with NDK **29.0.14206865** (the one Aurora's prebuilt Dawn is built with),
  `platforms;android-35`, `build-tools;35.0.1` and `cmake;3.31.6` (which also brings Ninja).
- JDK 17, Python 3 and a POSIX shell (on Windows, Git Bash).
- An internet connection the first time: CMake downloads SDL3, Aurora's prebuilt Dawn and a few
  smaller libraries, and Gradle downloads itself.
- The Khronos OpenXR loader for Android, **1.1.63**
  ([`openxr_loader_for_android-1.1.63.aar`](https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/1.1.63/)
  on Maven Central), unpacked into a folder with `include/openxr/*.h` and
  `lib/arm64-v8a/libopenxr_loader.so`. Point `STRIKERS_OPENXR_ROOT` at that folder.

```sh
sdkmanager "ndk;29.0.14206865" "platforms;android-35" "build-tools;35.0.1" "cmake;3.31.6"

# The OpenXR loader: an .aar is a zip.
export STRIKERS_OPENXR_ROOT="$HOME/openxr"
mkdir -p "$STRIKERS_OPENXR_ROOT/include" "$STRIKERS_OPENXR_ROOT/lib/arm64-v8a"
unzip -q openxr_loader_for_android-1.1.63.aar -d openxr-aar
cp -r "$(dirname "$(find openxr-aar -path '*include/openxr/openxr.h' | head -1)")" \
      "$STRIKERS_OPENXR_ROOT/include/"
cp "$(find openxr-aar -path '*arm64-v8a*' -name libopenxr_loader.so | head -1)" \
   "$STRIKERS_OPENXR_ROOT/lib/arm64-v8a/"

# The game, then the app.
export ANDROID_SDK_ROOT="/path/to/android-sdk"
export PATH="$ANDROID_SDK_ROOT/cmake/3.31.6/bin:$PATH"
cd smstrikers-port
tools/build-android.sh                   # -> build-android/jniLibs/arm64-v8a/
cd android && ./gradlew assembleDebug    # -> app/build/outputs/apk/debug/app-debug.apk
```

Notes:

- `STRIKERS_OPENXR_ROOT` is read when the build is first configured. If you set it after a failed
  first try, delete `build-android/` and run the script again.
- The APK on the Releases page is this debug variant, signed with a development key. A build of
  your own is signed with your own key, so it won't install over that one without an uninstall
  first.
- `PYTHON` and `CMAKE` in the environment override `python3` and `cmake` if yours have other names.
- The celebration shaders are checked in already compiled (`src/platform/xr_fx_shaders.h`). Only if
  you change one under `src/platform/shaders/`, rebuild them with `tools/compile-xr-shaders.ps1`
  and the NDK's `glslc`.
- The Quest code lives in `src/platform/xr_quest.cpp` (OpenXR session, input, view modes),
  `xr_layers.cpp` (what goes on the table and what goes in the stadium), `xr_fx.cpp` (celebrations),
  `xr_net.cpp` with `android/.../NetWs.java` (online), and `android/.../LobbyActivity.java` (the
  panel). The phone port's notes are in
  [`smstrikers-port/README-android.md`](smstrikers-port/README-android.md).

## The room server

Online play goes through a tiny relay: a Cloudflare Worker with one Durable Object per room code,
which passes each message to everyone in the room, one more per Version and release that pairs up
Quick match players, and one per Version and release that runs the tournaments. Its source is in [`server/`](server/), with
instructions to run your own copy on Cloudflare's free plan and point the game at it (one line,
`kServerHost` in `smstrikers-port/src/platform/xr_net.cpp`). Both players need builds that point at
the same server.

## Credits

- [Yannick Suter](https://github.com/yannicksuter) and contributors: the decompilation.
- [`new-coke/strikers`](https://github.com/new-coke/strikers): the native port this is built on.
- [`luisxl15/Strikers-Android-Port`](https://github.com/luisxl15/Strikers-Android-Port) (Luis
  Santos): the Android port this fork starts from.
- [encounter/aurora](https://github.com/encounter/aurora): GX/VI/PAD/CARD over WebGPU and SDL3.
- [JoeyAW/aurora-vr](https://github.com/JoeyAW/aurora-vr) and [JoeyAW/TPVR](https://github.com/JoeyAW/TPVR):
  the external render-pass and Vulkan/OpenXR hand-off that the headset path follows.
- [bylaws/libadrenotools](https://github.com/bylaws/libadrenotools): custom GPU driver loading,
  inherited from the Android port and still built in.
- [SDL](https://github.com/libsdl-org/SDL), [Dawn](https://dawn.googlesource.com/dawn),
  [zstd](https://github.com/facebook/zstd) (reads `.rvz`), [Opus](https://opus-codec.org) (voice chat),
  [rcheevos](https://github.com/RetroAchievements/rcheevos) (achievements) and the
  [Khronos OpenXR loader](https://github.com/KhronosGroup/OpenXR-SDK).
- [RetroAchievements](https://retroachievements.org) and its community: the Super Mario Strikers
  achievement set, used as they publish it.
- Button prompt artwork by [Kenney](https://www.kenney.nl) (CC0).
- Quest mixed-reality port and the Quest 3 Strikers art: [LanceSweatpants](https://github.com/LanceSweatpants).

## License

There is no single license for this repository. This is what applies, as far as it can be backed
up:

- **This fork's Quest additions** (the OpenXR layer, the celebrations, the online play and its room
  server in `server/`, the panel, and every other change on top of `luisxl15/Strikers-Android-Port`)
  are free software under the **GNU General Public License, version 3 or (at your option) any later
  version**. The text is in [`LICENSE`](LICENSE). The `.rvz` reader in
  `smstrikers-port/src/platform/disc.c` follows Dolphin's code and is GPL-2.0-or-later, used here
  under GPLv3; GPLv3 is also what lets it sit in one binary with the Apache-2.0 and FreeType-licensed
  libraries the APK links.
- **Third-party code compiled into the APK** and the notices its licenses ask for are listed in
  [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
- **The porting code, tools and docs from `new-coke/strikers`** are offered under CC0 1.0, as that
  project's README states. The text is in
  [`smstrikers-port/LICENSE-CC0.txt`](smstrikers-port/LICENSE-CC0.txt).
- **The reconstructed game code** (the decompilation) is an unofficial reconstruction. It may
  remain subject to third-party rights; no ownership of those is claimed, and none is granted here.
- **ODE physics** (`src/ode`, `include/ode`): ODE's BSD-style license
  ([`LICENSE-BSD.TXT`](smstrikers-port/LICENSE-BSD.TXT), and `src/ode/LICENSE` for what it covers).
  ODE's own headers also offer the LGPL 2.1, whose text is in
  [`LICENSE-LGPL-2.1.txt`](smstrikers-port/LICENSE-LGPL-2.1.txt).
- **`LICENSE-GPL-2.0.txt`** came with the upstream tree, which does not say which files it covers.
  zstd, below, offers the GPLv2 as one of its two options.
- **Bundled libraries**, each under its own file: Aurora (MIT, `extern/aurora/LICENSE`), MusyX (MIT,
  `extern/musyx/LICENSE`), zstd (BSD, `extern/zstd/LICENSE`), and Kenney's button prompts (CC0,
  `assets/input-prompts/LICENSE-Kenney.txt`). SDL3 (zlib), Dawn (BSD 3-Clause), libadrenotools
  (BSD 2-Clause), the OpenXR loader (Apache 2.0), Opus (BSD 3-Clause), rcheevos (MIT) and Aurora's other dependencies (abseil, fmt,
  FreeType, Dear ImGui, libpng, SQLite, xxHash) are downloaded at build time, each under its own
  license.
- **The Android port's own code** (`luisxl15/Strikers-Android-Port`) has no license stated by its
  author. It is included as published there, with credit above.

## Disclaimer

An unofficial project, not affiliated with or endorsed by Nintendo or Next Level Games. Super Mario
Strikers is a trademark of Nintendo. No game files are included in this repository or in the APK;
the game runs only from a disc image of your own, legally obtained copy.
