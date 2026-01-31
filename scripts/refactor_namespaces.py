#!/usr/bin/env python3
"""
Namespace refactoring script for mmo-4.

Renames src/common/ -> src/protocol/ and updates all namespaces to follow
the directory structure under an `mmo` root namespace.

Target mapping:
  src/protocol/         -> mmo::protocol
  src/engine/           -> mmo::engine
  src/engine/gpu/       -> mmo::engine::gpu
  src/engine/render/    -> mmo::engine::render
  src/engine/scene/     -> mmo::engine::scene
  src/engine/systems/   -> mmo::engine::systems
  src/client/           -> mmo::client
  src/client/ecs/       -> mmo::client::ecs
  src/server/           -> mmo::server
  src/server/ecs/       -> mmo::server::ecs
  src/server/systems/   -> mmo::server::systems
"""

import os
import re
import shutil
import sys

SRC = os.path.join(os.path.dirname(__file__), '..', 'src')
SRC = os.path.normpath(SRC)
ROOT = os.path.normpath(os.path.join(SRC, '..'))


def path_to_namespace(filepath: str) -> str:
    """Determine the target namespace for a file based on its directory."""
    rel = os.path.relpath(filepath, SRC)
    parts = rel.replace('\\', '/').split('/')
    # Remove filename
    dirs = parts[:-1]
    if not dirs:
        return 'mmo'
    # Map directory path to namespace
    return 'mmo::' + '::'.join(dirs)


def get_all_sources() -> list[str]:
    """Get all .hpp and .cpp files under src/."""
    sources = []
    for dirpath, _, filenames in os.walk(SRC):
        for f in filenames:
            if f.endswith(('.hpp', '.cpp')):
                sources.append(os.path.join(dirpath, f))
    return sorted(sources)


# ---- Phase 1: Rename src/common -> src/protocol ----

def rename_common_to_protocol():
    common_dir = os.path.join(SRC, 'common')
    protocol_dir = os.path.join(SRC, 'protocol')
    if os.path.isdir(common_dir) and not os.path.isdir(protocol_dir):
        print(f"Renaming {common_dir} -> {protocol_dir}")
        shutil.move(common_dir, protocol_dir)
    elif os.path.isdir(protocol_dir):
        print("src/protocol/ already exists, skipping rename")
    else:
        print("WARNING: src/common/ not found")


def update_includes_common_to_protocol():
    """Replace all #include "common/..." with #include "protocol/..." """
    for filepath in get_all_sources():
        with open(filepath, 'r') as f:
            content = f.read()
        new_content = content.replace('"common/', '"protocol/')
        if new_content != content:
            print(f"  Updated includes: {os.path.relpath(filepath, ROOT)}")
            with open(filepath, 'w') as f:
                f.write(new_content)


def update_cmake_common_to_protocol():
    """Update CMakeLists.txt references from common to protocol."""
    root_cmake = os.path.join(ROOT, 'CMakeLists.txt')
    with open(root_cmake, 'r') as f:
        content = f.read()
    new_content = content.replace('add_subdirectory(src/common)', 'add_subdirectory(src/protocol)')
    if new_content != content:
        print(f"  Updated {os.path.relpath(root_cmake, ROOT)}")
        with open(root_cmake, 'w') as f:
            f.write(new_content)

    # Rename the library target too
    proto_cmake = os.path.join(SRC, 'protocol', 'CMakeLists.txt')
    if os.path.exists(proto_cmake):
        with open(proto_cmake, 'r') as f:
            content = f.read()
        new_content = content.replace('mmo_common', 'mmo_protocol')
        if new_content != content:
            print(f"  Updated {os.path.relpath(proto_cmake, ROOT)}")
            with open(proto_cmake, 'w') as f:
                f.write(new_content)

    # Update all CMakeLists that reference mmo_common
    for dirpath, _, filenames in os.walk(ROOT):
        for f in filenames:
            if f == 'CMakeLists.txt':
                fpath = os.path.join(dirpath, f)
                with open(fpath, 'r') as fh:
                    content = fh.read()
                new_content = content.replace('mmo_common', 'mmo_protocol')
                if new_content != content and fpath != proto_cmake:
                    print(f"  Updated {os.path.relpath(fpath, ROOT)}")
                    with open(fpath, 'w') as fh:
                        fh.write(new_content)


# ---- Phase 2: Rewrite namespaces in each file ----

# Files that use nested namespace style: namespace mmo { namespace engine { ... } }
# vs compact style: namespace mmo::engine { ... }
# We'll normalize everything to compact C++17 style.

# Special inner namespaces that should stay nested WITHIN their file's namespace
# (these are organizational sub-namespaces, not directory-based)
INNER_NAMESPACES = {
    'fog', 'lighting', 'ui_colors', 'heightmap_config',
    'heightmap_generator', 'detail', 'CollisionLayers',
}


def rewrite_file_namespaces(filepath: str):
    """Rewrite namespace declarations in a file to match its directory."""
    target_ns = path_to_namespace(filepath)
    target_ns_parts = target_ns.split('::')

    with open(filepath, 'r') as f:
        content = f.read()

    original = content

    # Skip main.cpp files - they don't have namespace declarations of their own
    basename = os.path.basename(filepath)
    if basename == 'main.cpp':
        return

    lines = content.split('\n')
    new_lines = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        # Handle forward declaration blocks like: namespace mmo::gpu { class X; }
        # or namespace JPH { ... } - leave external namespaces alone
        fwd_match = re.match(r'^namespace\s+(JPH)\s*\{', stripped)
        if fwd_match:
            new_lines.append(line)
            i += 1
            continue

        # Match forward-declaration namespace blocks for our own code
        # e.g., namespace mmo::gpu { class GPUDevice; }
        fwd_block_match = re.match(r'^namespace\s+(mmo(?:::\w+)*)\s*\{$', stripped)
        if fwd_block_match:
            old_ns = fwd_block_match.group(1)
            # Check if this is a forward declaration block (short, contains only fwd decls)
            # Look ahead to find the closing brace
            block_lines = [line]
            j = i + 1
            is_fwd_block = False
            while j < len(lines):
                block_lines.append(lines[j])
                if lines[j].strip().startswith('}'):
                    # Check if all inner lines are forward declarations or empty
                    inner = [l.strip() for l in block_lines[1:-1] if l.strip()]
                    if all(re.match(r'^(class|struct)\s+\w+;$', l) for l in inner):
                        is_fwd_block = True
                    break
                j += 1

            if is_fwd_block:
                # Rewrite the forward declaration namespace
                new_ns = remap_old_namespace(old_ns, target_ns)
                new_lines.append(line.replace(f'namespace {old_ns}', f'namespace {new_ns}'))
                # Copy inner lines as-is
                for k in range(i + 1, j):
                    new_lines.append(lines[k])
                # Fix closing comment
                closing = lines[j]
                closing = re.sub(
                    r'}\s*//\s*namespace\s+\S+',
                    f'}} // namespace {new_ns}',
                    closing
                )
                new_lines.append(closing)
                i = j + 1
                continue

        # Match opening namespace - compact style: namespace mmo::systems {
        ns_compact = re.match(r'^namespace\s+(mmo(?:::\w+)*)\s*\{$', stripped)
        if ns_compact:
            old_ns = ns_compact.group(1)
            # Check if this is an inner/organizational namespace
            old_parts = old_ns.split('::')
            last_part = old_parts[-1]
            if last_part in INNER_NAMESPACES and old_ns != target_ns:
                # This is a sub-namespace within the file, keep the inner part only
                new_lines.append(line)
                i += 1
                continue

            new_ns = remap_old_namespace(old_ns, target_ns)
            new_line = line.replace(f'namespace {old_ns}', f'namespace {new_ns}')
            new_lines.append(new_line)
            i += 1
            continue

        # Match opening namespace - non-compact: namespace mmo {
        # followed later by namespace engine {
        ns_outer = re.match(r'^namespace\s+(mmo)\s*\{$', stripped)
        if ns_outer:
            # Look ahead to see if there's a nested namespace immediately (within a few lines)
            inner_ns = None
            inner_idx = None
            for j in range(i + 1, min(i + 10, len(lines))):
                inner_stripped = lines[j].strip()
                if inner_stripped == '' or inner_stripped.startswith('//') or inner_stripped.startswith('/*'):
                    continue
                inner_match = re.match(r'^namespace\s+(\w+)\s*\{$', inner_stripped)
                if inner_match:
                    candidate = inner_match.group(1)
                    if candidate not in INNER_NAMESPACES:
                        inner_ns = candidate
                        inner_idx = j
                break

            if inner_ns:
                # Merge into compact namespace: namespace mmo::engine -> target_ns
                old_ns = f'mmo::{inner_ns}'
                new_ns = remap_old_namespace(old_ns, target_ns)
                indent = line[:len(line) - len(line.lstrip())]
                new_lines.append(f'{indent}namespace {new_ns} {{')
                # Skip lines between outer and inner opening
                # But preserve any forward declarations / comments between them
                for k in range(i + 1, inner_idx):
                    fwd_line = lines[k].strip()
                    if fwd_line and not fwd_line.startswith('//'):
                        # Forward declarations that were in the outer namespace
                        # but before the inner - these need to move or be qualified
                        new_lines.append(lines[k])
                # Skip the inner namespace line itself
                i = inner_idx + 1
                continue
            else:
                # Plain namespace mmo { with no inner namespace
                new_ns = target_ns
                new_line = line.replace('namespace mmo', f'namespace {new_ns}')
                new_lines.append(new_line)
                i += 1
                continue

        # Match closing namespace comments
        close_match = re.match(r'^(\s*)}\s*//\s*namespace\s+(\S+)\s*$', line)
        if close_match:
            indent = close_match.group(1)
            old_ns = close_match.group(2)

            # Check if this is an inner namespace
            old_parts = old_ns.split('::')
            if old_parts[-1] in INNER_NAMESPACES:
                new_lines.append(line)
                i += 1
                continue

            new_ns = remap_old_namespace(old_ns, target_ns)
            new_lines.append(f'{indent}}} // namespace {new_ns}')
            i += 1
            continue

        # Match closing of merged nested namespace (e.g. "} // namespace engine" that
        # was the inner part of a nested pair - needs to become the target closing)
        close_inner = re.match(r'^(\s*)}\s*//\s*namespace\s+(\w+)\s*$', line)
        if close_inner:
            ns_name = close_inner.group(2)
            indent = close_inner.group(1)
            if ns_name in ('engine', 'gpu', 'systems', 'ecs', 'config'):
                # This was the inner namespace of a nested pair
                new_lines.append(f'{indent}}} // namespace {target_ns}')
                i += 1
                continue

        new_lines.append(line)
        i += 1

    content = '\n'.join(new_lines)

    # Now handle the case where we merged nested namespaces but left an extra
    # closing brace for the outer `namespace mmo {`
    # After merging `namespace mmo { ... namespace engine {` into `namespace mmo::engine {`,
    # there will be an orphaned `} // namespace mmo` at the end.
    content = remove_orphaned_outer_close(content, target_ns)

    if content != original:
        print(f"  Rewrote namespaces: {os.path.relpath(filepath, ROOT)}")
        with open(filepath, 'w') as f:
            f.write(content)


def remap_old_namespace(old_ns: str, target_ns: str) -> str:
    """Map an old namespace to the new target based on directory structure."""
    # Direct mappings for known old namespaces
    mappings = {
        'mmo': target_ns,
        'mmo::engine': target_ns if 'engine' in target_ns else target_ns,
        'mmo::gpu': target_ns.replace('mmo::engine', 'mmo::engine::gpu') if 'engine' in target_ns else 'mmo::engine::gpu',
        'mmo::systems': target_ns,
        'mmo::ecs': target_ns,
        'mmo::config': target_ns,
    }

    if old_ns in mappings:
        return mappings[old_ns]

    # For namespaces that already match the target pattern, keep as-is
    if old_ns == target_ns:
        return target_ns

    return target_ns


def remove_orphaned_outer_close(content: str, target_ns: str) -> str:
    """Remove orphaned } // namespace mmo lines left after merging nested namespaces."""
    lines = content.split('\n')
    # Count namespace opens and closes
    ns_opens = 0
    ns_closes = 0
    to_remove = []

    for i, line in enumerate(lines):
        stripped = line.strip()
        if re.match(r'^namespace\s+', stripped) and '{' in stripped:
            ns_opens += 1
        if re.match(r'^\}\s*//\s*namespace', stripped):
            ns_closes += 1

    if ns_closes > ns_opens:
        # Find the orphaned close - it's usually `} // namespace mmo` at the end
        for i in range(len(lines) - 1, -1, -1):
            if re.match(r'^\}\s*//\s*namespace\s+mmo\s*$', lines[i].strip()):
                lines.pop(i)
                ns_closes -= 1
                if ns_closes == ns_opens:
                    break

    return '\n'.join(lines)


# ---- Phase 3: Update qualified name references ----

def update_cross_references():
    """Update qualified references like engine::Application, systems::PhysicsSystem, etc."""
    replacements_by_context = {
        # In client files, references to engine types need engine:: prefix to still work
        # since client is now mmo::client, not mmo
        'client': [
            # engine:: already works since it's a sibling namespace
            # but references to types that were in mmo:: and are now in other namespaces need updating
            ('ecs::AttackEffect', 'client::ecs::AttackEffect'),
            ('ui_colors::', 'engine::ui_colors::'),
            ('fog::', 'engine::fog::'),
            ('lighting::', 'engine::lighting::'),
        ],
        'server': [
            ('ecs::', 'server::ecs::'),
            ('systems::', 'server::systems::'),
            ('config::', 'server::config::'),
            ('heightmap_config::', 'protocol::heightmap_config::'),
        ],
    }

    for filepath in get_all_sources():
        rel = os.path.relpath(filepath, SRC).replace('\\', '/')
        parts = rel.split('/')
        if not parts:
            continue

        top_dir = parts[0]
        target_ns = path_to_namespace(filepath)

        with open(filepath, 'r') as f:
            content = f.read()
        original = content

        # Universal replacements for include-path based types
        # Types that moved from mmo:: to specific namespaces and are referenced
        # from other namespaces need full qualification

        # Within engine files: types that were in mmo:: but are now in engine sub-namespaces
        if top_dir == 'engine':
            # render/ scene/ systems/ files may reference each other
            # gpu:: stays gpu:: within engine (it becomes engine::gpu but referenced as gpu:: within engine)
            pass

        if content != original:
            print(f"  Updated references: {os.path.relpath(filepath, ROOT)}")
            with open(filepath, 'w') as f:
                f.write(content)


# ---- Phase 4: Add using declarations where needed ----

def add_using_declarations():
    """
    After namespace changes, some files will need using declarations
    to bring sibling namespace types into scope. We handle the most
    critical ones here.
    """
    # Files in client/ that reference engine types without qualification
    # Since Game was in mmo:: and referenced RenderScene, UIScene etc (also in mmo::),
    # now Game is in mmo::client but those are in mmo::engine::scene etc.
    # Rather than add using declarations everywhere, we'll handle this in the build fix phase.
    pass


# ---- Main ----

def main():
    print("=" * 60)
    print("MMO-4 Namespace Refactoring")
    print("=" * 60)

    print("\n--- Phase 1: Rename common -> protocol ---")
    rename_common_to_protocol()
    update_includes_common_to_protocol()
    update_cmake_common_to_protocol()

    print("\n--- Phase 2: Rewrite namespace declarations ---")
    for filepath in get_all_sources():
        rewrite_file_namespaces(filepath)

    print("\n--- Phase 3: Update cross-references ---")
    update_cross_references()

    print("\n--- Done ---")
    print("Run a build to find remaining issues that need manual fixes.")
    print("Common issues will be:")
    print("  - Types referenced across namespace boundaries need full qualification")
    print("  - Forward declarations may need namespace updates")
    print("  - using declarations may be needed in some files")


if __name__ == '__main__':
    main()
