#!/usr/bin/env python3
"""
Generate 3D mountain models as GLB files.
Creates realistic mountain geometry with proper normals and vertex colors.
"""

import json
import struct
import numpy as np
from pathlib import Path


def write_glb(path, gltf, bin_data):
    """Write a GLB file from JSON and binary data."""
    json_str = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    # Pad JSON to 4-byte alignment
    while len(json_str) % 4 != 0:
        json_str += b' '
    
    # Pad binary to 4-byte alignment
    bin_data = bytearray(bin_data)
    while len(bin_data) % 4 != 0:
        bin_data += b'\x00'
    
    total_length = 12 + 8 + len(json_str) + 8 + len(bin_data)
    
    with open(path, 'wb') as f:
        # Header
        f.write(struct.pack('<III', 0x46546C67, 2, total_length))
        # JSON chunk
        f.write(struct.pack('<II', len(json_str), 0x4E4F534A))
        f.write(json_str)
        # Binary chunk
        f.write(struct.pack('<II', len(bin_data), 0x004E4942))
        f.write(bytes(bin_data))


def normalize(v):
    """Normalize a vector."""
    length = np.sqrt(np.sum(v * v))
    if length > 0:
        return v / length
    return v


def compute_triangle_normal(v0, v1, v2):
    """Compute the normal of a triangle."""
    edge1 = np.array(v1) - np.array(v0)
    edge2 = np.array(v2) - np.array(v0)
    normal = np.cross(edge1, edge2)
    return normalize(normal)


def height_to_color(height, max_height):
    """Get mountain color based on height (rock to snow)."""
    h = height / max_height
    
    # Dark rock at base, lighter rock mid, snow at peaks
    dark_rock = np.array([0.15, 0.13, 0.12])
    mid_rock = np.array([0.25, 0.23, 0.22])
    light_rock = np.array([0.35, 0.33, 0.32])
    snow = np.array([0.85, 0.88, 0.92])
    
    if h < 0.3:
        color = dark_rock + (mid_rock - dark_rock) * (h / 0.3)
    elif h < 0.6:
        color = mid_rock + (light_rock - mid_rock) * ((h - 0.3) / 0.3)
    elif h < 0.75:
        color = light_rock + (snow - light_rock) * ((h - 0.6) / 0.15)
    else:
        color = snow
    
    return color


def generate_mountain_mesh(base_radius, peak_height, segments=16, rings=8, variation_seed=42):
    """
    Generate a realistic mountain mesh with multiple elevation rings.
    Returns vertices, normals, colors, and indices.
    """
    np.random.seed(variation_seed)
    
    vertices = []
    normals = []
    colors = []
    indices = []
    
    # Generate heightmap rings from base to peak
    ring_heights = []
    ring_radii = []
    
    for r in range(rings + 1):
        t = r / rings  # 0 at base, 1 at peak
        
        # Exponential falloff for natural mountain shape
        ring_radius = base_radius * (1.0 - t ** 0.7)
        ring_height = peak_height * t
        
        # Add some variation to each ring
        angle_offsets = []
        radii_variation = []
        for s in range(segments):
            angle = (s / segments) * 2 * np.pi
            # Multi-frequency noise for natural look
            noise = (
                0.15 * np.sin(angle * 3 + variation_seed) +
                0.08 * np.sin(angle * 7 + variation_seed * 2.3) +
                0.05 * np.sin(angle * 13 + variation_seed * 3.7)
            )
            radii_variation.append(1.0 + noise * (1 - t * 0.5))  # Less variation at top
            
            # Height variation
            height_noise = 0.1 * np.sin(angle * 5 + variation_seed * 1.5) * (1 - t)
            angle_offsets.append(height_noise)
        
        ring_heights.append((ring_height, angle_offsets))
        ring_radii.append((ring_radius, radii_variation))
    
    # Create vertices for each ring
    for r in range(rings + 1):
        base_height, height_offsets = ring_heights[r]
        base_radius, radius_variations = ring_radii[r]
        
        for s in range(segments):
            angle = (s / segments) * 2 * np.pi
            rad_var = radius_variations[s]
            height_var = height_offsets[s]
            
            x = np.cos(angle) * base_radius * rad_var
            z = np.sin(angle) * base_radius * rad_var
            y = base_height + height_var * peak_height
            
            vertices.append([x, y, z])
            
            # Get color based on height
            color = height_to_color(y, peak_height)
            # Add some random variation
            color = color + np.random.uniform(-0.02, 0.02, 3)
            color = np.clip(color, 0, 1)
            colors.append([color[0], color[1], color[2], 1.0])
    
    # Add peak vertex
    peak_vertex = [0, peak_height, 0]
    vertices.append(peak_vertex)
    peak_color = height_to_color(peak_height, peak_height)
    colors.append([peak_color[0], peak_color[1], peak_color[2], 1.0])
    peak_idx = len(vertices) - 1
    
    # Add center base vertex (for bottom cap)
    vertices.append([0, 0, 0])
    base_color = height_to_color(0, peak_height)
    colors.append([base_color[0], base_color[1], base_color[2], 1.0])
    base_center_idx = len(vertices) - 1
    
    # Calculate normals (will be computed per-face then averaged)
    vertex_normals = [np.array([0.0, 0.0, 0.0]) for _ in vertices]
    vertex_normal_counts = [0 for _ in vertices]
    
    # Generate triangles between rings
    for r in range(rings):
        for s in range(segments):
            next_s = (s + 1) % segments
            
            # Indices in the current and next ring
            curr_ring_base = r * segments
            next_ring_base = (r + 1) * segments
            
            i0 = curr_ring_base + s
            i1 = curr_ring_base + next_s
            i2 = next_ring_base + s
            i3 = next_ring_base + next_s
            
            # Two triangles per quad
            indices.extend([i0, i2, i1])
            indices.extend([i1, i2, i3])
            
            # Compute face normals and add to vertices
            n1 = compute_triangle_normal(vertices[i0], vertices[i2], vertices[i1])
            n2 = compute_triangle_normal(vertices[i1], vertices[i2], vertices[i3])
            
            for idx in [i0, i2, i1]:
                vertex_normals[idx] = vertex_normals[idx] + n1
                vertex_normal_counts[idx] += 1
            for idx in [i1, i2, i3]:
                vertex_normals[idx] = vertex_normals[idx] + n2
                vertex_normal_counts[idx] += 1
    
    # Connect top ring to peak
    top_ring_base = rings * segments
    for s in range(segments):
        next_s = (s + 1) % segments
        i0 = top_ring_base + s
        i1 = top_ring_base + next_s
        
        indices.extend([i0, peak_idx, i1])
        
        n = compute_triangle_normal(vertices[i0], vertices[peak_idx], vertices[i1])
        for idx in [i0, peak_idx, i1]:
            vertex_normals[idx] = vertex_normals[idx] + n
            vertex_normal_counts[idx] += 1
    
    # Bottom cap (optional - helps with rendering)
    for s in range(segments):
        next_s = (s + 1) % segments
        i0 = s
        i1 = next_s
        
        indices.extend([base_center_idx, i0, i1])
        
        n = compute_triangle_normal(vertices[base_center_idx], vertices[i0], vertices[i1])
        for idx in [base_center_idx, i0, i1]:
            vertex_normals[idx] = vertex_normals[idx] + n
            vertex_normal_counts[idx] += 1
    
    # Average and normalize normals
    for i in range(len(vertices)):
        if vertex_normal_counts[i] > 0:
            normals.append(normalize(vertex_normals[i]).tolist())
        else:
            normals.append([0, 1, 0])  # Default up
    
    return vertices, normals, colors, indices


def create_mountain_glb(output_path, base_radius=1.0, peak_height=1.5, segments=20, rings=10, variation_seed=42):
    """Create a complete GLB file for a mountain model."""
    
    vertices, normals, colors, indices = generate_mountain_mesh(
        base_radius, peak_height, segments, rings, variation_seed
    )
    
    # Build binary buffer
    bin_data = bytearray()
    
    # Pack position data
    pos_offset = len(bin_data)
    for v in vertices:
        bin_data.extend(struct.pack('<fff', v[0], v[1], v[2]))
    pos_length = len(bin_data) - pos_offset
    
    # Align to 4 bytes
    while len(bin_data) % 4 != 0:
        bin_data.append(0)
    
    # Pack normal data
    norm_offset = len(bin_data)
    for n in normals:
        bin_data.extend(struct.pack('<fff', n[0], n[1], n[2]))
    norm_length = len(bin_data) - norm_offset
    
    # Align to 4 bytes
    while len(bin_data) % 4 != 0:
        bin_data.append(0)
    
    # Pack color data
    color_offset = len(bin_data)
    for c in colors:
        bin_data.extend(struct.pack('<ffff', c[0], c[1], c[2], c[3]))
    color_length = len(bin_data) - color_offset
    
    # Align to 4 bytes
    while len(bin_data) % 4 != 0:
        bin_data.append(0)
    
    # Pack index data
    index_offset = len(bin_data)
    for i in indices:
        bin_data.extend(struct.pack('<I', i))
    index_length = len(bin_data) - index_offset
    
    # Calculate bounds
    positions = np.array(vertices)
    min_pos = positions.min(axis=0).tolist()
    max_pos = positions.max(axis=0).tolist()
    
    # Create glTF structure
    gltf = {
        "asset": {"version": "2.0", "generator": "MMO Mountain Generator"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "Mountain"}],
        "meshes": [{
            "primitives": [{
                "attributes": {
                    "POSITION": 0,
                    "NORMAL": 1,
                    "COLOR_0": 2
                },
                "indices": 3,
                "mode": 4  # TRIANGLES
            }],
            "name": "Mountain"
        }],
        "accessors": [
            {
                "bufferView": 0,
                "componentType": 5126,  # FLOAT
                "count": len(vertices),
                "type": "VEC3",
                "min": min_pos,
                "max": max_pos
            },
            {
                "bufferView": 1,
                "componentType": 5126,  # FLOAT
                "count": len(normals),
                "type": "VEC3"
            },
            {
                "bufferView": 2,
                "componentType": 5126,  # FLOAT
                "count": len(colors),
                "type": "VEC4"
            },
            {
                "bufferView": 3,
                "componentType": 5125,  # UNSIGNED_INT
                "count": len(indices),
                "type": "SCALAR"
            }
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_offset, "byteLength": pos_length, "target": 34962},
            {"buffer": 0, "byteOffset": norm_offset, "byteLength": norm_length, "target": 34962},
            {"buffer": 0, "byteOffset": color_offset, "byteLength": color_length, "target": 34962},
            {"buffer": 0, "byteOffset": index_offset, "byteLength": index_length, "target": 34963}
        ],
        "buffers": [{"byteLength": len(bin_data)}]
    }
    
    write_glb(output_path, gltf, bytes(bin_data))
    print(f"Created {output_path}")
    print(f"  Vertices: {len(vertices)}, Triangles: {len(indices) // 3}")
    print(f"  Bounds: ({min_pos[0]:.2f}, {min_pos[1]:.2f}, {min_pos[2]:.2f}) to ({max_pos[0]:.2f}, {max_pos[1]:.2f}, {max_pos[2]:.2f})")


def main():
    output_dir = Path(__file__).parent.parent / "assets" / "models"
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Generate different mountain variations
    # Small mountain - for nearby details
    create_mountain_glb(
        output_dir / "mountain_small.glb",
        base_radius=1.0,
        peak_height=1.2,
        segments=12,
        rings=6,
        variation_seed=42
    )
    
    # Medium mountain - standard background
    create_mountain_glb(
        output_dir / "mountain_medium.glb",
        base_radius=1.0,
        peak_height=1.8,
        segments=16,
        rings=8,
        variation_seed=123
    )
    
    # Large mountain - dramatic peaks in distance
    create_mountain_glb(
        output_dir / "mountain_large.glb",
        base_radius=1.0,
        peak_height=2.5,
        segments=20,
        rings=10,
        variation_seed=456
    )
    
    # Extra tall peak - for distant backdrop
    create_mountain_glb(
        output_dir / "mountain_peak.glb",
        base_radius=0.8,
        peak_height=3.0,
        segments=24,
        rings=12,
        variation_seed=789
    )
    
    print("\nAll mountain models generated successfully!")


if __name__ == "__main__":
    main()
