#!/usr/bin/env python3
# /// script
# dependencies = [
#   "pillow",
#   "numpy",
# ]
# ///
"""
Generate placeholder terrain textures for the multi-texture terrain system.
Creates dirt, rock, sand textures and a default splatmap.
"""

from PIL import Image, ImageDraw
import numpy as np
import os

def create_simple_texture(width, height, base_color, noise_amount=0.1):
    """Create a simple texture with color variation."""
    img = Image.new('RGB', (width, height))
    pixels = []

    for y in range(height):
        for x in range(width):
            # Add some noise for variation
            r = int(base_color[0] * (1.0 + np.random.uniform(-noise_amount, noise_amount)))
            g = int(base_color[1] * (1.0 + np.random.uniform(-noise_amount, noise_amount)))
            b = int(base_color[2] * (1.0 + np.random.uniform(-noise_amount, noise_amount)))

            # Clamp values
            r = max(0, min(255, r))
            g = max(0, min(255, g))
            b = max(0, min(255, b))

            pixels.append((r, g, b))

    img.putdata(pixels)
    return img

def create_default_splatmap(width, height):
    """Create default splatmap - all grass (R=255, G=B=A=0)."""
    img = Image.new('RGBA', (width, height))
    pixels = []

    for y in range(height):
        for x in range(width):
            # All grass by default
            pixels.append((255, 0, 0, 0))

    img.putdata(pixels)
    return img

def main():
    # Create output directory if needed
    output_dir = "assets/textures"
    os.makedirs(output_dir, exist_ok=True)

    print("Generating terrain textures...")

    # Texture size
    size = 512

    # Create grass texture (green)
    print("  Creating grass_seamless.png...")
    grass = create_simple_texture(size, size, (75, 140, 60), noise_amount=0.15)
    grass.save(os.path.join(output_dir, "grass_seamless.png"))

    # Create dirt texture (brown)
    print("  Creating dirt_seamless.png...")
    dirt = create_simple_texture(size, size, (139, 90, 43), noise_amount=0.15)
    dirt.save(os.path.join(output_dir, "dirt_seamless.png"))

    # Create rock texture (gray)
    print("  Creating rock_seamless.png...")
    rock = create_simple_texture(size, size, (128, 128, 128), noise_amount=0.2)
    rock.save(os.path.join(output_dir, "rock_seamless.png"))

    # Create sand texture (tan/yellow)
    print("  Creating sand_seamless.png...")
    sand = create_simple_texture(size, size, (238, 214, 175), noise_amount=0.1)
    sand.save(os.path.join(output_dir, "sand_seamless.png"))

    # Create default splatmap (1024x1024 for higher resolution control)
    print("  Creating terrain_splatmap.png (1024x1024)...")
    splatmap = create_default_splatmap(1024, 1024)
    splatmap.save(os.path.join(output_dir, "terrain_splatmap.png"))

    print("\n✓ All textures created successfully!")
    print(f"  Location: {os.path.abspath(output_dir)}")
    print("\nYou can now:")
    print("  1. Run the game/editor - terrain should render with default grass")
    print("  2. Use the editor's splatmap painting tool to paint materials")
    print("  3. Replace these placeholder textures with real seamless textures")

if __name__ == "__main__":
    main()
