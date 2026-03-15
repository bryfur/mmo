#!/usr/bin/env python3
"""Generate normal maps from diffuse/color textures.

Usage:
    python3 tools/generate_normal_map.py <input> [output] [--strength N]

Examples:
    python3 tools/generate_normal_map.py assets/textures/new_grass.jpeg
    python3 tools/generate_normal_map.py assets/textures/new_grass.jpeg assets/textures/new_grass_normal.png --strength 2.0

If output is omitted, appends '_normal.png' to the input name.
Strength controls bump intensity (default 1.5). Higher = more pronounced bumps.
"""

import argparse
import os
import sys
import numpy as np
from PIL import Image


def generate_normal_map(image: np.ndarray, strength: float = 1.5) -> np.ndarray:
    """Generate a normal map from a grayscale height image.

    Uses Sobel-like gradient computation to derive surface normals.
    """
    # Convert to float grayscale [0, 1]
    if image.ndim == 3:
        # Luminance weights
        gray = (0.299 * image[:, :, 0] +
                0.587 * image[:, :, 1] +
                0.114 * image[:, :, 2])
    else:
        gray = image.astype(np.float64)

    gray = gray / 255.0

    # Compute gradients using Sobel operator
    # Sobel X kernel:  [-1 0 1]    Sobel Y kernel:  [-1 -2 -1]
    #                  [-2 0 2]                      [ 0  0  0]
    #                  [ 1 0 1]                      [ 1  2  1]
    # Using numpy roll for efficient tiled (wrapping) sampling
    top_left = np.roll(np.roll(gray, 1, axis=0), 1, axis=1)
    top = np.roll(gray, 1, axis=0)
    top_right = np.roll(np.roll(gray, 1, axis=0), -1, axis=1)
    left = np.roll(gray, 1, axis=1)
    right = np.roll(gray, -1, axis=1)
    bottom_left = np.roll(np.roll(gray, -1, axis=0), 1, axis=1)
    bottom = np.roll(gray, -1, axis=0)
    bottom_right = np.roll(np.roll(gray, -1, axis=0), -1, axis=1)

    # Sobel gradients
    dx = (top_right + 2.0 * right + bottom_right) - (top_left + 2.0 * left + bottom_left)
    dy = (bottom_left + 2.0 * bottom + bottom_right) - (top_left + 2.0 * top + top_right)

    dx *= strength
    dy *= strength

    # Construct normal vectors (OpenGL convention: Y-up in tangent space)
    nx = -dx
    ny = -dy
    nz = np.ones_like(nx)

    # Normalize
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx /= length
    ny /= length
    nz /= length

    # Map [-1, 1] -> [0, 255]
    normal_map = np.zeros((gray.shape[0], gray.shape[1], 3), dtype=np.uint8)
    normal_map[:, :, 0] = ((nx * 0.5 + 0.5) * 255).clip(0, 255).astype(np.uint8)
    normal_map[:, :, 1] = ((ny * 0.5 + 0.5) * 255).clip(0, 255).astype(np.uint8)
    normal_map[:, :, 2] = ((nz * 0.5 + 0.5) * 255).clip(0, 255).astype(np.uint8)

    return normal_map


def main():
    parser = argparse.ArgumentParser(description="Generate normal maps from diffuse textures")
    parser.add_argument("input", help="Input texture file")
    parser.add_argument("output", nargs="?", default=None, help="Output normal map file (default: <input>_normal.png)")
    parser.add_argument("--strength", type=float, default=1.5, help="Bump strength (default: 1.5)")
    args = parser.parse_args()

    if not os.path.isfile(args.input):
        print(f"Error: '{args.input}' not found", file=sys.stderr)
        sys.exit(1)

    # Default output name
    if args.output is None:
        base, _ = os.path.splitext(args.input)
        args.output = base + "_normal.png"

    print(f"Loading: {args.input}")
    img = Image.open(args.input)
    img_array = np.array(img)

    print(f"Generating normal map (strength={args.strength}, size={img.width}x{img.height})")
    normal_map = generate_normal_map(img_array, args.strength)

    Image.fromarray(normal_map).save(args.output)
    print(f"Saved: {args.output}")


if __name__ == "__main__":
    main()
