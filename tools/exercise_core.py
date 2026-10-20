"""Exercise the real libretro DLL with a locally supplied ROM and scripted pads.

Example: python tools/exercise_core.py --script "420:-|-;12:START|START;120:-|-"
Writes captures and diagnostics only inside --output, never beside the source ROM.
"""
import argparse
import ctypes as c
import hashlib
import json
import pathlib
import struct
import time
import zipfile

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUTTONS = {"B": 0, "SELECT": 2, "START": 3, "UP": 4, "DOWN": 5,
           "LEFT": 6, "RIGHT": 7, "A": 8, "L": 10, "R": 11}
ENV = c.CFUNCTYPE(c.c_bool, c.c_uint, c.c_void_p)
VIDEO = c.CFUNCTYPE(None, c.c_void_p, c.c_uint, c.c_uint, c.c_size_t)
AUDIO = c.CFUNCTYPE(c.c_size_t, c.c_void_p, c.c_size_t)
POLL = c.CFUNCTYPE(None)
INPUT = c.CFUNCTYPE(c.c_int16, c.c_uint, c.c_uint, c.c_uint, c.c_uint)


class Game(c.Structure):
    _fields_ = [("path", c.c_char_p), ("data", c.c_void_p),
                ("size", c.c_size_t), ("meta", c.c_char_p)]


class Variable(c.Structure):
    _fields_ = [("key", c.c_char_p), ("value", c.c_char_p)]


class Message(c.Structure):
    _fields_ = [("msg", c.c_char_p), ("frames", c.c_uint)]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--core", type=pathlib.Path, default=ROOT / "build-windows/mgba/Release/kirby_coop_libretro.dll")
    p.add_argument("--rom", type=pathlib.Path, required=True)
    p.add_argument("--script", default="420:-|-;12:START|-;120:-|-;12:-|START;120:-|-")
    p.add_argument("--output", type=pathlib.Path, default=ROOT / "test-output")
    p.add_argument("--load-state", type=pathlib.Path)
    p.add_argument("--load-sram", type=pathlib.Path)
    p.add_argument("--layout", default="Side by side")
    p.add_argument("--verify-state", action="store_true")
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lib = c.CDLL(str(args.core.resolve()))
    pressed = [set(), set()]
    errors, captures = [], []
    screen = [None]
    audio_frames = [0]
    values = {b"kirby_coop_layout": args.layout.encode(), b"kirby_coop_audio": b"Player 1"}

    @ENV
    def env(cmd, data):
        if cmd == 15:  # GET_VARIABLE
            var = c.cast(data, c.POINTER(Variable)).contents
            var.value = values.get(var.key)
            return bool(var.value)
        if cmd == 17:  # GET_VARIABLE_UPDATE
            c.cast(data, c.POINTER(c.c_bool))[0] = False
            return True
        if cmd == 6:
            msg = c.cast(data, c.POINTER(Message)).contents.msg.decode(errors="replace")
            errors.append(msg)
            print("CORE MESSAGE:", msg, flush=True)
            return True
        if cmd in (10, 16, 11, 35, 37):
            return True
        return False

    @VIDEO
    def video(data, width, height, pitch):
        if data:
            screen[0] = (width, height, pitch, c.string_at(data, height * pitch))

    @AUDIO
    def audio(data, count):
        audio_frames[0] += count
        return count

    @POLL
    def poll():
        pass

    @INPUT
    def inp(port, device, index, button):
        return int(port < 2 and button in pressed[port])

    for name, cb in [("environment", env), ("video_refresh", video),
                     ("audio_sample_batch", audio), ("input_poll", poll), ("input_state", inp)]:
        fn = getattr(lib, "retro_set_" + name)
        fn.argtypes = [type(cb)]
        fn(cb)
    lib.retro_load_game.argtypes = [c.POINTER(Game)]
    lib.retro_load_game.restype = c.c_bool
    lib.retro_serialize_size.restype = c.c_size_t
    lib.retro_serialize.argtypes = [c.c_void_p, c.c_size_t]
    lib.retro_serialize.restype = c.c_bool
    lib.retro_unserialize.argtypes = [c.c_void_p, c.c_size_t]
    lib.retro_unserialize.restype = c.c_bool
    lib.retro_get_memory_size.argtypes = [c.c_uint]
    lib.retro_get_memory_size.restype = c.c_size_t
    lib.retro_get_memory_data.argtypes = [c.c_uint]
    lib.retro_get_memory_data.restype = c.c_void_p
    if args.rom.suffix.lower() == ".zip":
        with zipfile.ZipFile(args.rom) as z:
            rom = z.read(next(n for n in z.namelist() if n.lower().endswith(".gba")))
    else:
        rom = args.rom.read_bytes()
    buf = c.create_string_buffer(rom)
    game = Game(str(args.rom).encode(), c.cast(buf, c.c_void_p), len(rom), None)
    lib.retro_init()
    assert lib.retro_load_game(c.byref(game)), "Core rejected ROM"
    if args.load_sram:
        sram = args.load_sram.read_bytes()
        assert len(sram) == lib.retro_get_memory_size(0)
        c.memmove(lib.retro_get_memory_data(0), sram, len(sram))
    if args.load_state:
        state = args.load_state.read_bytes()
        state_buf = c.create_string_buffer(state)
        assert lib.retro_unserialize(state_buf, len(state)), "Could not load linked state"

    def capture(name):
        assert screen[0], "No video callback"
        w, h, pitch, raw = screen[0]
        rgb = bytearray()
        for y in range(h):
            for (v,) in struct.iter_unpack("<H", raw[y*pitch:y*pitch+w*2]):
                rgb.extend((((v >> 11) & 31) * 255 // 31,
                            ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
        image = Image.frombytes("RGB", (w, h), bytes(rgb))
        image.resize((w*3, h*3), Image.Resampling.NEAREST).save(args.output / (name + ".png"))
        record = {"name": name, "width": w, "height": h, "sha256": hashlib.sha256(raw).hexdigest()}
        captures.append(record)
        print(json.dumps(record), flush=True)

    start = time.monotonic()
    frames = 0
    for i, action in enumerate(args.script.split(";")):
        count, pads = action.split(":")
        for port, names in enumerate(pads.split("|")):
            pressed[port] = {BUTTONS[n] for n in names.split("+") if n != "-"}
        for _ in range(int(count)):
            lib.retro_run()
            frames += 1
            if errors:
                raise RuntimeError(errors[-1])
        capture(f"step-{i:02}")
    elapsed = time.monotonic() - start
    size = lib.retro_serialize_size()
    state_buf = c.create_string_buffer(size)
    assert lib.retro_serialize(state_buf, size), "Saving paired state failed"
    (args.output / "session.state").write_bytes(state_buf.raw)
    (args.output / "session.srm").write_bytes(c.string_at(lib.retro_get_memory_data(0), lib.retro_get_memory_size(0)))
    if args.verify_state:
        pressed[:] = [set(), set()]
        for _ in range(120):
            lib.retro_run()
        expected = screen[0][3]
        assert lib.retro_unserialize(state_buf, size), "Loading paired state failed"
        for _ in range(120):
            lib.retro_run()
        assert screen[0][3] == expected, "Paired state replay diverged"
        print("Paired save-state deterministic replay passed", flush=True)
    lib.retro_unload_game()
    lib.retro_deinit()
    report = {"rom_sha1": hashlib.sha1(rom).hexdigest(), "frames": frames,
              "elapsed_seconds": round(elapsed, 3), "emulated_fps": round(frames/elapsed, 2),
              "audio_frames": audio_frames[0], "errors": errors, "captures": captures}
    (args.output / "report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
