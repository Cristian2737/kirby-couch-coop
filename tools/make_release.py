"""Validate and package public core builds, docs, notices, and HEAD's source.

Only explicitly supplied libraries and a fixed documentation allowlist enter
the binary ZIP. ROMs, local profiles, logs, and saves are never collected.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REQUIRED_API = {
    "retro_init", "retro_deinit", "retro_api_version", "retro_set_environment",
    "retro_set_video_refresh", "retro_set_audio_sample", "retro_set_audio_sample_batch",
    "retro_set_input_poll", "retro_set_input_state", "retro_get_system_info",
    "retro_get_system_av_info", "retro_set_controller_port_device", "retro_reset",
    "retro_run", "retro_serialize_size", "retro_serialize", "retro_unserialize",
    "retro_cheat_reset", "retro_cheat_set", "retro_load_game", "retro_load_game_special",
    "retro_unload_game", "retro_get_region", "retro_get_memory_data", "retro_get_memory_size",
}
DOCUMENTS = (
    "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "UPSTREAM.md",
    "docs/SETUP.md", "docs/HOW-IT-WORKS.md", "docs/BUILDING.md",
    "docs/INITIAL-BUILD-CHECKS.json", "docs/images/two-player-preview.png",
    "licenses/inih.txt",
)


def inspect_binary(path, platform):
    data = path.read_bytes()
    result = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    if platform == "windows-x64":
        assert data[:2] == b"MZ", "Expected a Windows PE binary"
        peoff = struct.unpack_from("<I", data, 0x3C)[0]
        assert data[peoff:peoff+4] == b"PE\0\0"
        assert struct.unpack_from("<H", data, peoff+4)[0] == 0x8664, "Expected x64"
        return result | {"architecture": "Windows x64"}

    assert data[:4] == b"\x7fELF" and data[5] == 1, "Expected little-endian ELF"
    bits = data[4]
    machine = struct.unpack_from("<H", data, 18)[0]
    assert machine == (183 if platform == "android-arm64" else 40), "Wrong Android architecture"
    if bits == 2:
        phoff, shoff = struct.unpack_from("<QQ", data, 32)
        phsize, phnum, shsize, shnum = struct.unpack_from("<HHHH", data, 54)
        sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff+i*shsize) for i in range(shnum)]
        symformat, alignformat, alignoff = "<IBBHQQ", "<Q", 48
    else:
        assert bits == 1
        phoff, shoff = struct.unpack_from("<II", data, 28)
        phsize, phnum, shsize, shnum = struct.unpack_from("<HHHH", data, 42)
        sections = [struct.unpack_from("<IIIIIIIIII", data, shoff+i*shsize) for i in range(shnum)]
        symformat, alignformat, alignoff = "<IIIBBH", "<I", 28
    alignments = [
        struct.unpack_from(alignformat, data, phoff+i*phsize+alignoff)[0]
        for i in range(phnum)
        if struct.unpack_from("<I", data, phoff+i*phsize)[0] == 1
    ]
    assert alignments and min(alignments) >= 16384, "Expected 16 KiB LOAD alignment"
    exports = set()
    for section in sections:
        if section[1] != 11:  # SHT_DYNSYM
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4]+strings[5]]
        for off in range(section[4], section[4]+section[5], section[9]):
            symbol = struct.unpack_from(symformat, data, off)
            defined = symbol[3] != 0 if bits == 2 else symbol[5] != 0
            if defined:
                exports.add(names[symbol[0]:].split(b"\0", 1)[0].decode())
    assert REQUIRED_API <= exports, f"Missing libretro exports: {REQUIRED_API-exports}"
    return result | {"elf_class": 64 if bits == 2 else 32, "machine": machine,
                     "load_alignment": alignments, "libretro_exports": len(REQUIRED_API)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows", type=Path)
    parser.add_argument("--arm64", type=Path)
    parser.add_argument("--armv7", type=Path)
    parser.add_argument("--version", default="0.1")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--no-source", action="store_true", help="Skip git archive for an extracted source tree")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.version):
        parser.error("Version must be a filename-safe release identifier")
    supplied = [("windows-x64", args.windows, ".dll"),
                ("android-arm64", args.arm64, ".so"),
                ("android-armv7", args.armv7, ".so")]
    supplied = [(platform, path.resolve(), suffix) for platform, path, suffix in supplied if path]
    if not supplied:
        parser.error("Supply at least one platform library")
    checks = {platform: inspect_binary(path, platform) for platform, path, _ in supplied}
    args.output.mkdir(parents=True, exist_ok=True)
    stem = f"Kirby-Couch-Coop-{args.version}"
    binary_path = args.output / (stem + ".zip")
    with zipfile.ZipFile(binary_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for platform, path, suffix in supplied:
            archive.write(path, f"{platform}/kirby_coop_libretro{suffix}")
            archive.write(ROOT / "kirby_coop_libretro.info", f"{platform}/kirby_coop_libretro.info")
        for relative in DOCUMENTS:
            archive.write(ROOT / relative, relative)
        archive.writestr("BUILD-CHECKS.json", json.dumps(checks, indent=2) + "\n")
    with zipfile.ZipFile(binary_path) as archive:
        assert archive.testzip() is None
    if not args.no_source:
        source_path = args.output / (stem + "-source.zip")
        subprocess.run(["git", "-c", f"safe.directory={ROOT.as_posix()}", "archive",
                        "--format=zip", "HEAD", "-o", str(source_path)], cwd=ROOT, check=True)
        with zipfile.ZipFile(source_path) as archive:
            assert archive.testzip() is None
        print(source_path)
    print(binary_path)
    print(json.dumps(checks, indent=2))


if __name__ == "__main__":
    main()
