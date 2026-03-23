#!/usr/bin/env python3
"""Convert interleaved SC16 IQ (int16 I,Q) into FC32 complex64 binary."""

import argparse
from pathlib import Path

import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert raw interleaved SC16 IQ (int16 I,Q) to FC32 complex64 "
            "binary format."
        )
    )
    parser.add_argument(
        "input_file",
        type=Path,
        help="Path to source SC16 binary file",
    )
    parser.add_argument(
        "output_file",
        type=Path,
        nargs="?",
        help="Path to destination FC32 binary file (default: <input>_fc32.bin)",
    )
    parser.add_argument(
        "--scale",
        type=float,
        default=2048.0,
        help="Normalization divisor applied to I/Q samples (default: 2048.0)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.scale <= 0:
        raise ValueError("--scale must be > 0")

    in_path = args.input_file
    if not in_path.is_file():
        raise FileNotFoundError(f"Input file not found: {in_path}")

    if args.output_file is None:
        if in_path.suffix:
            out_name = f"{in_path.stem}_fc32{in_path.suffix}"
        else:
            out_name = f"{in_path.name}_fc32.bin"
        out_path = in_path.with_name(out_name)
    else:
        out_path = args.output_file

    raw = np.fromfile(in_path, dtype=np.int16)
    if raw.size % 2 != 0:
        raise ValueError(
            "Input file has odd number of int16 values; expected interleaved I,Q pairs"
        )

    iq = raw.reshape(-1, 2).astype(np.float32)
    fc32 = (iq[:, 0] + 1j * iq[:, 1]) / args.scale
    fc32 = fc32.astype(np.complex64)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    fc32.tofile(out_path)

    print(f"Input: {in_path}")
    print(f"Output: {out_path}")
    print(f"Complex samples: {fc32.size}")
    print(f"Scale: {args.scale}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
