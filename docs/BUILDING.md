# Build and development dependencies

The downloaded cores do not require these development tools. This page is
for compiling the supplied source or running the scripted test harness.

## Dependencies

| Tool/component | Used for | Version/status |
| --- | --- | --- |
| CMake | Configuring and building all targets | 3.16+; original builds used 4.4.4 |
| C and C++ toolchain | Building mGBA and the custom frontend | C11 source; Windows used MSVC 19.43 / VS 2022 |
| Windows SDK | Windows platform headers/libraries | Original Windows build used SDK 10.0.19041.0 |
| Android NDK | Android compiler, sysroot, platform libraries | Original Android builds used 23.1.7779620 (r23b) |
| Ninja | Android build generator | Must be on PATH for the examples below |
| Python | Optional testing and release packaging | 3.10+; original harness used 3.12 |
| Pillow | Optional screenshot/test harness only | Original harness used 11.3.0; see `requirements-dev.txt` |
| Git | Cloning source and making source release archives | Normal Git command-line installation |
| mGBA | Emulator implementation and GBA link coordinator | Complete pinned source included under `mgba/` |
| inih | Embedded configuration parser compiled with mGBA | Vendored by mGBA; BSD license notice included |

The core is compiled as a libretro library; it does **not** link the RetroArch
application into the library. RetroArch is a runtime frontend and an integration
test dependency. The libretro header is included in the vendored source.

Qt, SDL, FFmpeg, PNG, Lua, OpenGL rendering, and other optional upstream features
are disabled or not used by this minimal frontend. You do not need their SDKs
to reproduce the listed build targets. Platform system libraries and the
selected compiler's runtime remain dependencies of each native binary.

The vendored mGBA source is already patched. No submodule download or network
fetch occurs during the root CMake configure. See [UPSTREAM.md](../UPSTREAM.md)
for the exact original revision and the local change.

## Windows x64

Install Visual Studio 2022 with **Desktop development with C++**, a Windows SDK,
and CMake. From the repository root:

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Release --target mgba_libretro --parallel 4
```

Output:

```text
build-windows/mgba/Release/kirby_coop_libretro.dll
```

Install it alongside the root `kirby_coop_libretro.info` as described in the
[player guide](SETUP.md). When distributing an MSVC build, document the
Microsoft Visual C++ v14 x64 runtime requirement.

## Android ARM64 and ARMv7

Install an Android NDK and Ninja. The commands below use the NDK path supplied
by you; they do not depend on an Android Studio or Unity directory.

PowerShell, for ARM64:

```powershell
$ndkRoot = 'C:/Android/android-ndk-r23b'
cmake -S . -B build-android-arm64 -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$ndkRoot/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 -DCMAKE_BUILD_TYPE=Release
cmake --build build-android-arm64 --target mgba_libretro --parallel 4
```

For ARMv7, use a separate build directory:

```powershell
cmake -S . -B build-android-armv7 -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$ndkRoot/build/cmake/android.toolchain.cmake" -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-21 -DCMAKE_BUILD_TYPE=Release
cmake --build build-android-armv7 --target mgba_libretro --parallel 4
```

Outputs:

```text
build-android-arm64/mgba/kirby_coop_libretro.so
build-android-armv7/mgba/kirby_coop_libretro.so
```

For a Unix shell, the corresponding ARM64 configure command is:

```sh
cmake -S . -B build-android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 -DCMAKE_BUILD_TYPE=Release
cmake --build build-android-arm64 --target mgba_libretro --parallel 4
```

The root build supplies `-Wl,-z,max-page-size=16384` for Android targets.
This produces 16 KiB-aligned ELF load segments; checking alignment alone does
not establish compatibility or performance on every Android device.

## Other platforms

The frontend uses portable libretro callbacks and mGBA platform abstractions.
Linux and macOS builds may be possible with a suitable C/C++ toolchain and
CMake, but no native builds for them are currently verified or distributed.
Do not assume a Linux or Windows library can be loaded on Android.

## Scripted verification with your local game

Install the test-only Python dependency:

```powershell
python -m pip install -r requirements-dev.txt
```

Then, with a locally supplied USA ROM:

```powershell
python tools/exercise_core.py --rom 'C:/Games/Your Amazing Mirror.gba' --verify-state
```

The default library path points to the Windows build. Use `--core` to point
to another library loadable by your current Python process. `.gba` and ZIP
input are supported by the harness. `--rom` is required; no game is downloaded.

The harness supplies libretro callbacks, drives independent scripted controller
inputs, saves screenshots and diagnostics under `test-output/`, and can compare
the video output of a 120-frame run before and after a paired state restore.
Use `--output` to choose another local directory.

The full initial connection sequence used in testing was:

```powershell
python tools/exercise_core.py --rom 'C:/Games/Your Amazing Mirror.gba' --output test-connected --script '1000:-|-;10:START|START;100:-|-;10:A|A;100:-|-;10:A|A;100:-|-;8:DOWN|DOWN;12:-|-;8:A|A;150:-|-;8:A|A;180:-|-;8:A|-;180:-|-;8:A|-;300:-|-' --verify-state
```

The script syntax is `frames:P1-buttons|P2-buttons`, with steps separated by
semicolons. `-` releases all buttons; `+` combines buttons, such as `RIGHT+A`.
Menu sequences depend on game progress and the starting screen.

This is a local developer tool, not the application players need to launch.
Do not commit the resulting ROM, SRAM, or state files. The main repository
ignore rules exclude those local artifacts.

## Release packaging

For the original platform builds, package the libraries and public setup docs:

```powershell
python tools/make_release.py --windows build-windows/mgba/Release/kirby_coop_libretro.dll --arm64 build-android-arm64/mgba/kirby_coop_libretro.so --armv7 build-android-armv7/mgba/kirby_coop_libretro.so
```

You can supply only the platforms you built. The standard-library-only packager
checks file formats, Android exported API symbols, and ELF alignment, then
includes the `.info` file, documentation, licenses, hashes, and preview. It uses
`git archive HEAD` to create a separate corresponding-source archive, so commit
the intended release source first. Outputs go under `dist/`.

The initial release's binary hashes are in
[INITIAL-BUILD-CHECKS.json](INITIAL-BUILD-CHECKS.json). Rebuilding may change
binary hashes because of compiler versions, timestamps, paths, and embedded
version metadata; this is not currently a bit-for-bit reproducible build setup.

## Validation status

- Original Windows DLL: two-player gameplay, independent movement/cameras,
  1,800 additional gameplay frames, paired state replay, layout/focus checks,
  and integration with RetroArch 1.19.1.
- Initial tester confirmed the PC co-op works interactively.
- Android ARM64 and ARMv7: compiled and inspected; physical-device testing is pending.
- CI builds use no commercial game ROM and therefore do not establish gameplay
  compatibility. They check that the public source compiles into native cores.

Please distinguish compilation, automated ROM-backed checks, and hands-on
device testing when reporting a new platform as supported.
