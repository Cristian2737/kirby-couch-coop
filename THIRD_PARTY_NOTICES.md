# Third-party notices

## mGBA

The emulator, GBA link implementation, platform utilities, and vendored libretro
header come from [mGBA](https://github.com/mgba-emu/mgba), originally authored by
Jeffrey Pfau / endrift and its contributors. mGBA files retain their original
copyright and license headers. The principal project license is the
[Mozilla Public License 2.0](LICENSE).

The corresponding source for the initial binaries is included in this public
repository and the source archive attached to the release. See [UPSTREAM.md](UPSTREAM.md)
for the exact original revision and the local queue-restoration modification.

## inih

The minimal core compiles mGBA's copy of inih, by Ben Hoyt. The complete New BSD
license text is included in [licenses/inih.txt](licenses/inih.txt) and accompanies
the binary distributions. The original copy is retained at
`mgba/src/third-party/inih/LICENSE.txt`.

## Other upstream notices

Other upstream source components retain their own copyright and license text
under `mgba/`. Most optional media, scripting, UI, and graphics components are
disabled in this project's build. mGBA's CRC32 implementation retains Gary S.
Brown's unrestricted-use notice; its MD5 and SHA1 sources retain their upstream
attribution notes. Consult those source headers when modifying or redistributing
individual components.

The game title and characters belong to their respective owners. This project
includes no commercial game ROM, BIOS, save file, or game assets extracted for
use as game content. The documentation preview is a gameplay screenshot.
