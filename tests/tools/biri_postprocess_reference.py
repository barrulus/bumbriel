#!/usr/bin/env python3
"""Extract the frozen Biri GLSL that umbrielfx-postprocess-test compares against.

Usage: biri_postprocess_reference.py <destination>; pass <destination> as the test's second argument.
"""

import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("destination", type=Path)
args = parser.parse_args()
original = Path(__file__).resolve().parents[2] / "docs/porting/biri-shaders/original/resources/shaders"
macros = "\n".join(
    f"#define niri_{name} umbriel_{name}"
    for name in ("time", "size", "output_size", "cursor", "region", "scale")
) + "\n#define global_color postprocess\n#define global_buffer postprocess_buffer\n"
count = 0
for scope in ("window", "screen", "cursor"):
    for path in sorted((original / scope).iterdir()):
        if path.suffix not in (".frag", ".kdl"):
            continue
        text = path.read_text()
        sources = [text] if path.suffix == ".frag" else re.findall(
            r'source\s+"((?:[^"\\]|\\.)*)"', text, re.S
        )
        if not sources:
            raise ValueError(f"No shader source in {path}")
        for index, source in enumerate(sources):
            suffix = f"-{index}" if len(sources) > 1 else ""
            target = args.destination / scope / f"{path.stem}{suffix}.glsl"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("// Biri reference; GPL v3, see original/LICENSE.\n" + macros + source)
            count += 1
for path in sorted((original.parents[2] / "window-source").glob("*.frag")):
    target = args.destination / "window" / f"{path.stem}.glsl"
    if not (original / "window" / path.name).exists():
        count += 1
    target.write_text("// Biri reference; GPL v3, see original/LICENSE.\n" + macros + path.read_text())
if count != 47:
    raise ValueError(f"Expected 47 pass sources, extracted {count}")
print(f"Extracted {count} reference pass sources into {args.destination}")
