#!/usr/bin/env python3
"""
Add a humanoid skeleton, skin weights, and animations to a static GLB model.
This is an automated approximation - results may need manual refinement in Blender.
"""

import json
import struct
import numpy as np
from pathlib import Path
import sys

def read_glb(path):
    """Read a GLB file and return the JSON and binary data."""
    with open(path, 'rb') as f:
        # GLB header
        magic, version, length = struct.unpack('<III', f.read(12))
        if magic != 0x46546C67:  # 'glTF'
            raise ValueError("Not a valid GLB file")
        
        # JSON chunk
        chunk_len, chunk_type = struct.unpack('<II', f.read(8))
        json_data = json.loads(f.read(chunk_len).decode('utf-8'))
        
        # Binary chunk (if present)
        bin_data = b''
        if f.tell() < length:
            chunk_len, chunk_type = struct.unpack('<II', f.read(8))
            bin_data = f.read(chunk_len)
        
        return json_data, bin_data

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

def get_accessor_data(gltf, bin_data, accessor_idx):
    """Extract data from an accessor."""
    accessor = gltf['accessors'][accessor_idx]
    buffer_view = gltf['bufferViews'][accessor['bufferView']]
    
    offset = buffer_view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    
    # Determine data type
    component_types = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
    dtype = component_types[accessor['componentType']]
    
    # Determine count per element
    type_counts = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
    count = type_counts[accessor['type']]
    
    total = accessor['count'] * count
    size = struct.calcsize(dtype) * total
    
    data = struct.unpack(f'<{total}{dtype}', bin_data[offset:offset + size])
    
    if count == 1:
        return list(data)
    return [data[i:i+count] for i in range(0, len(data), count)]

def add_to_buffer(bin_data, data, dtype='f'):
    """Add data to the binary buffer, return offset and byte length."""
    offset = len(bin_data)
    # Align to 4 bytes
    while offset % 4 != 0:
        bin_data += b'\x00'
        offset = len(bin_data)
    
    if dtype == 'f':
        packed = struct.pack(f'<{len(data)}f', *data)
    elif dtype == 'H':
        packed = struct.pack(f'<{len(data)}H', *data)
    elif dtype == 'B':
        packed = struct.pack(f'<{len(data)}B', *data)
    else:
        packed = data
    
    bin_data += packed
    return offset, len(packed)

def create_humanoid_skeleton(mesh_bounds):
    """Create a simple humanoid skeleton based on mesh bounds."""
    min_pt, max_pt = mesh_bounds
    
    # Mesh dimensions
    width = max_pt[0] - min_pt[0]
    height = max_pt[1] - min_pt[1]
    depth = max_pt[2] - min_pt[2]
    
    center_x = (min_pt[0] + max_pt[0]) / 2
    center_z = (min_pt[2] + max_pt[2]) / 2
    
    # Create bones from bottom to top (assuming Y-up)
    # Each bone: (name, position, parent_index)
    bones = [
        ("Root",       [center_x, min_pt[1], center_z], -1),
        ("Hips",       [center_x, min_pt[1] + height * 0.45, center_z], 0),
        ("Spine",      [center_x, min_pt[1] + height * 0.55, center_z], 1),
        ("Chest",      [center_x, min_pt[1] + height * 0.65, center_z], 2),
        ("Neck",       [center_x, min_pt[1] + height * 0.80, center_z], 3),
        ("Head",       [center_x, min_pt[1] + height * 0.90, center_z], 4),
        
        # Left arm
        ("LeftShoulder",  [center_x + width * 0.25, min_pt[1] + height * 0.75, center_z], 3),
        ("LeftArm",       [center_x + width * 0.35, min_pt[1] + height * 0.65, center_z], 6),
        ("LeftForearm",   [center_x + width * 0.40, min_pt[1] + height * 0.50, center_z], 7),
        ("LeftHand",      [center_x + width * 0.45, min_pt[1] + height * 0.35, center_z], 8),
        
        # Right arm
        ("RightShoulder", [center_x - width * 0.25, min_pt[1] + height * 0.75, center_z], 3),
        ("RightArm",      [center_x - width * 0.35, min_pt[1] + height * 0.65, center_z], 10),
        ("RightForearm",  [center_x - width * 0.40, min_pt[1] + height * 0.50, center_z], 11),
        ("RightHand",     [center_x - width * 0.45, min_pt[1] + height * 0.35, center_z], 12),
        
        # Left leg
        ("LeftUpLeg",     [center_x + width * 0.12, min_pt[1] + height * 0.45, center_z], 1),
        ("LeftLeg",       [center_x + width * 0.12, min_pt[1] + height * 0.25, center_z], 14),
        ("LeftFoot",      [center_x + width * 0.12, min_pt[1] + height * 0.05, center_z], 15),
        
        # Right leg
        ("RightUpLeg",    [center_x - width * 0.12, min_pt[1] + height * 0.45, center_z], 1),
        ("RightLeg",      [center_x - width * 0.12, min_pt[1] + height * 0.25, center_z], 17),
        ("RightFoot",     [center_x - width * 0.12, min_pt[1] + height * 0.05, center_z], 18),
    ]
    
    return bones

def compute_skin_weights(positions, bones, max_influences=4):
    """Compute automatic skin weights based on distance to bones."""
    num_verts = len(positions)
    num_bones = len(bones)
    
    joints = np.zeros((num_verts, max_influences), dtype=np.uint8)
    weights = np.zeros((num_verts, max_influences), dtype=np.float32)
    
    bone_positions = np.array([b[1] for b in bones])
    
    for i, pos in enumerate(positions):
        pos = np.array(pos)
        
        # Calculate distance to each bone
        distances = np.linalg.norm(bone_positions - pos, axis=1)
        
        # Convert to weights (inverse distance, with minimum)
        inv_dist = 1.0 / (distances + 0.001)
        
        # Get top N influences
        top_indices = np.argsort(inv_dist)[-max_influences:][::-1]
        top_weights = inv_dist[top_indices]
        
        # Normalize weights
        total = np.sum(top_weights)
        if total > 0:
            top_weights /= total
        
        joints[i] = top_indices
        weights[i] = top_weights
    
    return joints.flatten().tolist(), weights.flatten().tolist()

def create_animations(bones, mesh_height):
    """Create basic animations: idle, walk, attack."""
    animations = []
    
    # Animation parameters
    fps = 30
    
    # Idle animation - subtle breathing/swaying (2 seconds)
    idle_duration = 2.0
    idle_frames = int(idle_duration * fps)
    idle_times = [i / fps for i in range(idle_frames + 1)]
    
    # Walk animation (1 second cycle)
    walk_duration = 1.0
    walk_frames = int(walk_duration * fps)
    walk_times = [i / fps for i in range(walk_frames + 1)]
    
    # Attack animation (0.5 seconds)
    attack_duration = 0.5
    attack_frames = int(attack_duration * fps)
    attack_times = [i / fps for i in range(attack_frames + 1)]
    
    def quat_from_euler(rx, ry, rz):
        """Convert euler angles (radians) to quaternion [x,y,z,w]."""
        cx, sx = np.cos(rx/2), np.sin(rx/2)
        cy, sy = np.cos(ry/2), np.sin(ry/2)
        cz, sz = np.cos(rz/2), np.sin(rz/2)
        return [
            sx*cy*cz - cx*sy*sz,
            cx*sy*cz + sx*cy*sz,
            cx*cy*sz - sx*sy*cz,
            cx*cy*cz + sx*sy*sz
        ]
    
    identity_quat = [0, 0, 0, 1]
    
    # Bone indices
    HIPS, SPINE, CHEST = 1, 2, 3
    LEFT_ARM, LEFT_FOREARM = 7, 8
    RIGHT_ARM, RIGHT_FOREARM = 11, 12
    LEFT_UPLEG, LEFT_LEG = 14, 15
    RIGHT_UPLEG, RIGHT_LEG = 17, 18
    
    # === IDLE ANIMATION ===
    idle_channels = []
    
    # Subtle spine rotation
    spine_rotations = []
    for t in idle_times:
        angle = np.sin(t * np.pi) * 0.02  # Very subtle
        spine_rotations.extend(quat_from_euler(angle, 0, 0))
    idle_channels.append((SPINE, 'rotation', idle_times, spine_rotations))
    
    # === WALK ANIMATION ===
    walk_channels = []
    
    # Hip bob (up/down translation)
    hip_translations = []
    base_hip_y = bones[HIPS][1][1]
    for t in walk_times:
        phase = t / walk_duration * 2 * np.pi
        y_offset = np.abs(np.sin(phase * 2)) * 0.02 * mesh_height
        hip_translations.extend([0, y_offset, 0])
    walk_channels.append((HIPS, 'translation', walk_times, hip_translations))
    
    # Left leg swing
    left_leg_rotations = []
    for t in walk_times:
        phase = t / walk_duration * 2 * np.pi
        angle = np.sin(phase) * 0.5  # ~30 degree swing
        left_leg_rotations.extend(quat_from_euler(angle, 0, 0))
    walk_channels.append((LEFT_UPLEG, 'rotation', walk_times, left_leg_rotations))
    
    # Right leg swing (opposite phase)
    right_leg_rotations = []
    for t in walk_times:
        phase = t / walk_duration * 2 * np.pi
        angle = np.sin(phase + np.pi) * 0.5
        right_leg_rotations.extend(quat_from_euler(angle, 0, 0))
    walk_channels.append((RIGHT_UPLEG, 'rotation', walk_times, right_leg_rotations))
    
    # Arm swing (opposite to legs)
    left_arm_rotations = []
    right_arm_rotations = []
    for t in walk_times:
        phase = t / walk_duration * 2 * np.pi
        left_arm_rotations.extend(quat_from_euler(np.sin(phase + np.pi) * 0.3, 0, 0))
        right_arm_rotations.extend(quat_from_euler(np.sin(phase) * 0.3, 0, 0))
    walk_channels.append((LEFT_ARM, 'rotation', walk_times, left_arm_rotations))
    walk_channels.append((RIGHT_ARM, 'rotation', walk_times, right_arm_rotations))
    
    # === ATTACK ANIMATION ===
    attack_channels = []
    
    # Right arm swing attack
    right_arm_attack = []
    right_forearm_attack = []
    for t in attack_times:
        progress = t / attack_duration
        if progress < 0.3:
            # Wind up
            arm_angle = -progress / 0.3 * 1.5  # Pull back
            forearm_angle = -progress / 0.3 * 0.5
        elif progress < 0.5:
            # Swing
            swing_progress = (progress - 0.3) / 0.2
            arm_angle = -1.5 + swing_progress * 3.0  # Swing forward
            forearm_angle = -0.5 + swing_progress * 1.0
        else:
            # Follow through and return
            return_progress = (progress - 0.5) / 0.5
            arm_angle = 1.5 * (1 - return_progress)
            forearm_angle = 0.5 * (1 - return_progress)
        
        right_arm_attack.extend(quat_from_euler(arm_angle, 0, 0))
        right_forearm_attack.extend(quat_from_euler(forearm_angle, 0, 0))
    
    attack_channels.append((RIGHT_ARM, 'rotation', attack_times, right_arm_attack))
    attack_channels.append((RIGHT_FOREARM, 'rotation', attack_times, right_forearm_attack))
    
    # Slight torso rotation during attack
    chest_attack = []
    for t in attack_times:
        progress = t / attack_duration
        if progress < 0.3:
            angle = -progress / 0.3 * 0.3
        elif progress < 0.5:
            swing_progress = (progress - 0.3) / 0.2
            angle = -0.3 + swing_progress * 0.6
        else:
            return_progress = (progress - 0.5) / 0.5
            angle = 0.3 * (1 - return_progress)
        chest_attack.extend(quat_from_euler(0, angle, 0))
    attack_channels.append((CHEST, 'rotation', attack_times, chest_attack))
    
    return [
        ('Idle', idle_channels),
        ('Walk', walk_channels),
        ('Attack', attack_channels),
    ]

def add_skeleton_to_glb(input_path, output_path):
    """Add skeleton, skinning, and animations to a GLB file."""
    print(f"Reading {input_path}...")
    gltf, bin_data = read_glb(input_path)
    bin_data = bytearray(bin_data)
    
    # Find mesh and get positions
    mesh_node_idx = None
    for i, node in enumerate(gltf['nodes']):
        if 'mesh' in node:
            mesh_node_idx = i
            break
    
    if mesh_node_idx is None:
        raise ValueError("No mesh found in model")
    
    mesh_idx = gltf['nodes'][mesh_node_idx]['mesh']
    mesh = gltf['meshes'][mesh_idx]
    primitive = mesh['primitives'][0]
    
    # Get vertex positions
    pos_accessor_idx = primitive['attributes']['POSITION']
    positions = get_accessor_data(gltf, bin_data, pos_accessor_idx)
    
    print(f"Found {len(positions)} vertices")
    
    # Calculate bounds
    positions_np = np.array(positions)
    min_pt = positions_np.min(axis=0).tolist()
    max_pt = positions_np.max(axis=0).tolist()
    mesh_height = max_pt[1] - min_pt[1]
    
    print(f"Mesh bounds: {min_pt} to {max_pt}")
    
    # Create skeleton
    bones = create_humanoid_skeleton((min_pt, max_pt))
    print(f"Created {len(bones)} bones")
    
    # Compute skin weights
    print("Computing skin weights...")
    joints_data, weights_data = compute_skin_weights(positions, bones)
    
    # Create animations
    print("Creating animations...")
    animations = create_animations(bones, mesh_height)
    
    # Now modify the glTF structure
    base_accessor_idx = len(gltf['accessors'])
    base_buffer_view_idx = len(gltf['bufferViews'])
    base_node_idx = len(gltf['nodes'])
    
    # Add joint nodes
    joint_node_indices = []
    for i, (name, pos, parent_idx) in enumerate(bones):
        node = {
            'name': name,
            'translation': pos,
            'rotation': [0, 0, 0, 1],
            'scale': [1, 1, 1]
        }
        gltf['nodes'].append(node)
        joint_node_indices.append(base_node_idx + i)
    
    # Set up parent-child relationships
    for i, (name, pos, parent_idx) in enumerate(bones):
        if parent_idx >= 0:
            parent_node = gltf['nodes'][joint_node_indices[parent_idx]]
            if 'children' not in parent_node:
                parent_node['children'] = []
            parent_node['children'].append(joint_node_indices[i])
    
    # Compute inverse bind matrices
    inverse_bind_matrices = []
    for name, pos, parent_idx in bones:
        # Simple translation-only inverse bind matrix
        ibm = [
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            -pos[0], -pos[1], -pos[2], 1
        ]
        inverse_bind_matrices.extend(ibm)
    
    # Add inverse bind matrices to buffer
    ibm_offset, ibm_size = add_to_buffer(bin_data, inverse_bind_matrices, 'f')
    
    # Add buffer view for IBM
    ibm_buffer_view_idx = len(gltf['bufferViews'])
    gltf['bufferViews'].append({
        'buffer': 0,
        'byteOffset': ibm_offset,
        'byteLength': ibm_size
    })
    
    # Add accessor for IBM
    ibm_accessor_idx = len(gltf['accessors'])
    gltf['accessors'].append({
        'bufferView': ibm_buffer_view_idx,
        'componentType': 5126,  # FLOAT
        'count': len(bones),
        'type': 'MAT4'
    })
    
    # Add skin
    skin_idx = len(gltf.get('skins', []))
    if 'skins' not in gltf:
        gltf['skins'] = []
    
    gltf['skins'].append({
        'inverseBindMatrices': ibm_accessor_idx,
        'joints': joint_node_indices,
        'skeleton': joint_node_indices[0],
        'name': 'Armature'
    })
    
    # Add skin to mesh node
    gltf['nodes'][mesh_node_idx]['skin'] = skin_idx
    
    # Add JOINTS_0 attribute to mesh
    joints_offset, joints_size = add_to_buffer(bin_data, joints_data, 'B')
    joints_bv_idx = len(gltf['bufferViews'])
    gltf['bufferViews'].append({
        'buffer': 0,
        'byteOffset': joints_offset,
        'byteLength': joints_size,
        'byteStride': 4
    })
    joints_acc_idx = len(gltf['accessors'])
    gltf['accessors'].append({
        'bufferView': joints_bv_idx,
        'componentType': 5121,  # UNSIGNED_BYTE
        'count': len(positions),
        'type': 'VEC4'
    })
    primitive['attributes']['JOINTS_0'] = joints_acc_idx
    
    # Add WEIGHTS_0 attribute to mesh
    weights_offset, weights_size = add_to_buffer(bin_data, weights_data, 'f')
    weights_bv_idx = len(gltf['bufferViews'])
    gltf['bufferViews'].append({
        'buffer': 0,
        'byteOffset': weights_offset,
        'byteLength': weights_size,
        'byteStride': 16
    })
    weights_acc_idx = len(gltf['accessors'])
    gltf['accessors'].append({
        'bufferView': weights_bv_idx,
        'componentType': 5126,  # FLOAT
        'count': len(positions),
        'type': 'VEC4'
    })
    primitive['attributes']['WEIGHTS_0'] = weights_acc_idx
    
    # Add animations
    if 'animations' not in gltf:
        gltf['animations'] = []
    
    for anim_name, channels in animations:
        samplers = []
        anim_channels = []
        
        for bone_idx, path, times, values in channels:
            # Add time accessor
            times_offset, times_size = add_to_buffer(bin_data, times, 'f')
            times_bv_idx = len(gltf['bufferViews'])
            gltf['bufferViews'].append({
                'buffer': 0,
                'byteOffset': times_offset,
                'byteLength': times_size
            })
            times_acc_idx = len(gltf['accessors'])
            gltf['accessors'].append({
                'bufferView': times_bv_idx,
                'componentType': 5126,
                'count': len(times),
                'type': 'SCALAR',
                'min': [min(times)],
                'max': [max(times)]
            })
            
            # Add values accessor
            values_offset, values_size = add_to_buffer(bin_data, values, 'f')
            values_bv_idx = len(gltf['bufferViews'])
            gltf['bufferViews'].append({
                'buffer': 0,
                'byteOffset': values_offset,
                'byteLength': values_size
            })
            
            vec_type = 'VEC4' if path == 'rotation' else 'VEC3'
            values_acc_idx = len(gltf['accessors'])
            gltf['accessors'].append({
                'bufferView': values_bv_idx,
                'componentType': 5126,
                'count': len(times),
                'type': vec_type
            })
            
            # Add sampler
            sampler_idx = len(samplers)
            samplers.append({
                'input': times_acc_idx,
                'output': values_acc_idx,
                'interpolation': 'LINEAR'
            })
            
            # Add channel
            anim_channels.append({
                'sampler': sampler_idx,
                'target': {
                    'node': joint_node_indices[bone_idx],
                    'path': path
                }
            })
        
        gltf['animations'].append({
            'name': anim_name,
            'samplers': samplers,
            'channels': anim_channels
        })
    
    # Update buffer size
    gltf['buffers'][0]['byteLength'] = len(bin_data)
    
    # Add root skeleton node to scene
    root_skeleton_node = joint_node_indices[0]
    if 'scenes' in gltf and len(gltf['scenes']) > 0:
        if 'nodes' not in gltf['scenes'][0]:
            gltf['scenes'][0]['nodes'] = []
        if root_skeleton_node not in gltf['scenes'][0]['nodes']:
            gltf['scenes'][0]['nodes'].append(root_skeleton_node)
    
    # Write output
    print(f"Writing {output_path}...")
    write_glb(output_path, gltf, bytes(bin_data))
    
    print("Done!")
    print(f"  Bones: {len(bones)}")
    print(f"  Animations: {len(animations)}")
    for anim_name, channels in animations:
        print(f"    - {anim_name}: {len(channels)} channels")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python add_skeleton.py <input.glb> [output.glb]")
        print("  If output not specified, writes to <input>_rigged.glb")
        sys.exit(1)
    
    input_path = sys.argv[1]
    if len(sys.argv) >= 3:
        output_path = sys.argv[2]
    else:
        p = Path(input_path)
        output_path = str(p.parent / f"{p.stem}_rigged{p.suffix}")
    
    add_skeleton_to_glb(input_path, output_path)
