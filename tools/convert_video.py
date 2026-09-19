"""Convert a video into a bounded raw MJPEG stream for Cardputer Adv Hub."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("input", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--fps", type=int, default=12, choices=range(1, 21))
parser.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg executable or path")
args = parser.parse_args()
if not args.input.is_file():
    parser.error("Input file does not exist")
if args.output.suffix.lower() not in (".mjpeg", ".mjpg"):
    parser.error("Output must end in .mjpeg or .mjpg")
if args.output.exists():
    parser.error("Output already exists; choose a new filename")
subprocess.run([
    args.ffmpeg, "-nostdin", "-n", "-i", str(args.input), "-an",
    "-vf", f"fps={args.fps},scale=240:96:force_original_aspect_ratio=decrease,pad=240:96:(ow-iw)/2:(oh-ih)/2",
    "-c:v", "mjpeg", "-q:v", "6", "-f", "mjpeg", str(args.output)
], check=True)
print(f"Copy {args.output.name} into /video; set MJPEG speed to {args.fps} fps. Video has no audio.")
