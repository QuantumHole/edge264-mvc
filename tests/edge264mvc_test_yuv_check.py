#!/usr/bin/env python3
"""Check that edge264mvc_test compares every sample of a frame with its .yuv reference.

For each stream, the decoded frames (written with -o) serve as the reference
YUV, which must PASS. Then a single sample is changed at a corner of the first
or the last frame, in each plane, and every such change must FAIL - including
the right-hand columns and the bottom rows, and the corners of a cropped picture,
whose differing macroblock is then printed in part.
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

STREAMS = (
	Path("tests/conformance/2d/BA1_Sony_D.264"), # 176x144
	Path("tests/conformance/2d-synthetic/plane_without_d.264"), # 1920x1080, cropped from 1088
	Path("tests/conformance/2d-synthetic/crop_top_left.264"), # 48x32, cropped by 8 on every side
)


def run(exe, stream, timeout):
	return subprocess.run([exe, "-s", str(stream)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--exe", default="./edge264mvc_test")
	parser.add_argument("--timeout", type=float, default=60)
	args = parser.parse_args()
	exe = str(Path(args.exe).resolve())
	failures = 0
	with tempfile.TemporaryDirectory() as tmp:
		for source in STREAMS:
			stream = Path(tmp) / source.name
			shutil.copyfile(source, stream)
			y4m = subprocess.run([exe, "-s", "-o", str(source)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=args.timeout).stdout
			header, _, body = y4m.partition(b"\n")
			fields = dict((f[:1], f[1:]) for f in header.split()[1:])
			width, height = int(fields[b"W"]), int(fields[b"H"])
			frame_size = width * height * 3 // 2
			frames = [f[:frame_size] for f in body.split(b"FRAME\n")[1:]]
			if not frames or any(len(f) != frame_size for f in frames):
				print(f"FAIL {source}: could not read the decoded frames")
				failures += 1
				continue
			reference = b"".join(frames)
			yuv = stream.with_suffix(".yuv")
			yuv.write_bytes(reference)
			if run(exe, stream, args.timeout).returncode != 0:
				print(f"FAIL {source}: the decoded frames do not PASS as their own reference")
				failures += 1
				continue
			# the corners of each plane, in the first and the last frame
			planes = ((0, width, height), (width * height, width // 2, height // 2), (width * height * 5 // 4, width // 2, height // 2))
			for frame in (0, len(frames) - 1):
				for plane, (offset, w, h) in enumerate(planes):
					for x, y in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
						pos = frame * frame_size + offset + y * w + x
						changed = bytearray(reference)
						changed[pos] ^= 0x55
						yuv.write_bytes(changed)
						result = run(exe, stream, args.timeout).returncode
						if result != 1: # FAIL, after printing the macroblock, rather than PASS or a crash
							print(f"FAIL {source}: a change at frame {frame}, plane {plane}, x {x}, y {y} gives exit status {result}, not 1")
							failures += 1
			yuv.write_bytes(reference)
	if failures:
		print(f"edge264mvc_test YUV comparison: {failures} checks FAILED")
		return 1
	print(f"edge264mvc_test YUV comparison PASS ({len(STREAMS)} streams)")
	return 0


if __name__ == "__main__":
	sys.exit(main())
