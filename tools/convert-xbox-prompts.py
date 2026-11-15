"""Convert supplied PNGs to the SDK's packed, straight-alpha BGRA icon resource."""
import argparse
import struct
from pathlib import Path
from PIL import Image

NAMES = [
    "button_A", "button_Y", "button_B", "button_X",
    "analog_L_up", "analog_L_down", "analog_L_left", "analog_L_right",
    "button_LB", "button_start", "button_RB", "button_back",
    "analog_L_neutral", "dpad_up", "dpad_right", "dpad_down", "dpad_left",
    "analog_R_neutral", "analog_R_up", "analog_R_down",
    "analog_R_left", "analog_R_right",
]


def convert(source, output):
    data = bytearray(struct.pack("<4sIII", b"XBP1", 32, 32, len(NAMES)))
    for name in NAMES:
        with Image.open(source / (name + ".png")) as image:
            image = image.convert("RGBA")
            image.thumbnail((32, 32), Image.Resampling.LANCZOS)
            canvas = Image.new("RGBA", (32, 32))
            canvas.paste(image, ((32 - image.width) // 2, (32 - image.height) // 2))
            data.extend(canvas.tobytes("raw", "BGRA"))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, default=Path("src/core/xbox_prompts.bgra"))
    args = parser.parse_args()
    convert(args.source, args.output)
