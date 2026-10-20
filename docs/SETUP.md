# Player setup

## Download and choose a build

Download the ZIP from [Releases](https://github.com/Cristian2737/kirby-couch-coop/releases).
Extract it before installing. Each platform folder contains the core and its
matching `kirby_coop_libretro.info` file.

| Folder | Use with |
| --- | --- |
| `windows-x64` | 64-bit RetroArch on Windows |
| `android-arm64` | ARM64 / 64-bit RetroArch on Android |
| `android-armv7` | ARMv7 / 32-bit RetroArch on Android |

Android x86/x86-64 and Windows ARM64 native builds are not supplied.
The Android binaries target API 21 or newer. No Android device has yet been
verified for this release; a matching architecture alone does not establish
playable performance.

## Windows

1. In RetroArch, find **Settings → Directory → Cores**. Close RetroArch and copy
   `kirby_coop_libretro.dll` into that directory. Alternatively, use **Load Core →
   Install or Restore a Core** if your build exposes it.
2. Copy `kirby_coop_libretro.info` into the directory listed under
   **Settings → Directory → Core Info**. This metadata is needed for the
   displayed core name and RetroArch's save-state support.
3. Reopen RetroArch. Choose **Load Core → Kirby Couch Co-op (experimental)**,
   then **Load Content** and select your `.gba` ROM.
4. If the core loads only when **Show All Cores** is enabled, check the `.info`
   location and restart RetroArch. It may initially appear by its filename.

If Windows reports missing `VCRUNTIME140.dll` or related runtime files, install
the [Microsoft Visual C++ v14 x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
Download dependencies from their publishers rather than individual DLL sites.

### Optional command-line launch

Use your actual paths, for example in PowerShell:

```powershell
& 'C:\RetroArch\retroarch.exe' --libretro 'C:\RetroArch\cores\kirby_coop_libretro.dll' 'C:\Games\Kirby Amazing Mirror - Couch Co-op.gba'
```

RetroArch still needs the `.info` file in its configured Core Info directory.

## Android and Android TV, including ONN

An ONN stick's marketing name does not identify its RetroArch app architecture.
Select the ARM64 build for a 64-bit RetroArch app or ARMv7 for a 32-bit app.
If Android rejects the core, check your app/system architecture and try the
matching folder. A 64-bit processor does not imply a 64-bit Android installation.

1. Transfer the extracted core and `.info` file to a location your Android
   file picker and RetroArch can access, such as Downloads. The transfer method
   is up to you: a local file-transfer app, USB storage where supported, or a
   download directly on the device.
2. Open **Load Core → Install or Restore a Core**, then select the extracted
   `kirby_coop_libretro.so` file. See
   [Libretro's manual Android installation instructions](https://www.libretro.com/index.php/category/android/).
3. Copy `kirby_coop_libretro.info` into the directory shown under **Settings →
   Directory → Core Info**. If the directory is inaccessible, select a writable
   Core Info directory, populate it with **Online Updater → Update Core Info
   Files**, and add this file. Restart RetroArch afterward.
4. Load this core and then your game file. For first setup, use an extracted
   `.gba` file rather than relying on your file manager's ZIP handling.
5. Pair both controllers with Android before starting. Then configure their
   separate RetroArch ports as described below.

If the manual core installer is missing or restricted, consult your RetroArch
distribution's documentation; the release requires a frontend that permits
custom cores. Android permissions and menu labels vary by RetroArch version.

## Controllers

In **Settings → Input → RetroPad Binds**, or the corresponding **Port 1/2
Controls** menus in your version:

- **Port 1 → Device Index:** the first player's controller.
- **Port 2 → Device Index:** a different controller for player 2.
- Keep each player's mapped port distinct in **Quick Menu → Controls**.

Use RetroArch's usual controller mapping. The core maps the GBA D-pad, A, B,
Start, Select, L, and R separately for each player. GBA A jumps; GBA B inhales
or uses an ability. Both players need Start/A available to pass their menus.

Two controllers mapped to port 1 will control the same game. A controller
assigned only to port 2 will not advance player 1's title screen. The core
runs both games automatically, so opening a second RetroArch process is unnecessary.

## Start the adventure

1. Pass the title screen on both players' controllers.
2. Choose a file on each screen.
3. On each screen choose **Start Game → Multiplayer**. Choosing single player
   starts independent adventures rather than the shared co-op session.
4. Wait until both players appear on the connection screen.
5. Player 1 presses **A** to confirm **OK?/ENTRY**.
6. Player 1 selects the Game Pak whose adventure data will be used (P1 is the
   default) and presses **A** again.
7. Start can skip the opening sequence. Pink is player 1; yellow is player 2.

The two remaining Kirbys retain the game's original AI. Players can separate,
explore independently, and use the game's normal call mechanic.

## Display and audio

Set **Settings → Video → Scaling → Aspect Ratio → Core provided**. Disable
integer scaling if you want the largest proportional image that fits the TV.
Black bars are expected when the combined views do not match the TV's shape.

In **Quick Menu → Core Options**:

| Option | Behavior |
| --- | --- |
| Side by side | Two full 240×160 views, combined into 480×160 |
| Stacked | Two full views, combined into 240×320 |
| Player 1 fullscreen | Shows P1's full view; both games and controllers stay active |
| Player 2 fullscreen | Shows P2's full view; both games and controllers stay active |
| Game audio: Player 1 / Player 2 | Plays audio from one system to avoid duplicate music |

Side by side gives larger proportional views on a normal 16:9 television.
This core supplies one combined display output, not two separate monitor outputs.
It preserves the game's camera zoom and does not show extra level area.

## Saves and frontend features

Use a different ROM filename or separate save directory to avoid mixing this
core's saves with single-player mGBA saves. The custom `.srm` is 262,144 bytes:
two fixed 128 KiB cartridge-save slots. It is not an ordinary single-player save.

Save states capture both systems together. Save and load through RetroArch's
Quick Menu or your configured hotkeys. The `.info` file must be installed for
RetroArch to expose this support correctly. States are experimental; keep native
in-game saves as well. Save-state interchange between different builds/platforms
has not been verified.

Disable **run-ahead**, **preemptive frames**, **rewind**, and **RetroAchievements**.
Network play and cheats are not implemented by this project.

## Other emulators and frontends

This download is a **libretro core**. It is loaded by RetroArch in place of
the ordinary GBA core. It is not a `.gba` patch that can be loaded into any
GBA emulator. Standalone mGBA, VBA, and GBA hardware cannot load the distributed
`.dll`/`.so` directly.

Another libretro frontend would need to accept user-installed cores, supply
two independent input ports, accept RGB565 video and live geometry/audio-rate
updates, and expose core options and save RAM. Such frontends have not been tested.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Core will not load | Correct operating system and app architecture; missing Windows runtime; installer permits custom cores |
| Generic filename or no Save State menu | Install `.info` into Core Info directory; restart RetroArch and refresh core info |
| Only one player responds | Assign different controllers to ports 1 and 2; inspect mapped ports |
| Both screens show unrelated gameplay | Select Multiplayer on both screens rather than One Player |
| Waiting at connection screen | Enter Multiplayer on both screens; then confirm with P1 when both have joined |
| Joined but not starting | P1 must also choose which Game Pak's adventure data to use |
| Views are stretched or cut off | Core-provided aspect ratio; remove custom cropping/viewport overrides |
| Slow play or crackling audio | Disable shaders and latency features; two software-emulated GBAs may exceed the device's capacity |
| Link scheduler stalled message | Restart content; report build, device, frontend version, and reproduction steps |

If you report a bug, include your RetroArch version, its architecture, device,
chosen build, controller assignments, ROM SHA-1, and steps. Do not upload the
ROM or BIOS. [Open an issue](https://github.com/Cristian2737/kirby-couch-coop/issues).
