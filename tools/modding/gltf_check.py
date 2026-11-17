#!/usr/bin/env python3
"""Structural check of glTF 2.0 files (.gltf + external .bin), independent of b3modtool's own C++ check.

    tools/modding/gltf_check.py <file.gltf | folder> ...   [--quiet]

For every .gltf (folders are searched recursively): the JSON parses; asset.version is "2.0"; every index (buffer,
bufferView, accessor, mesh, material, texture, image, sampler, node, scene, KHR_materials_variants variant) is in range;
buffer 0's byteLength fits the .bin; each bufferView lies inside its buffer; each accessor lies inside its bufferView,
is aligned to its component size (vertex attributes to 4 bytes) and has the right min / max length; POSITION is VEC3
float with min / max equal to the data; no NaN or infinity in any float accessor; NORMAL is unit length; all attributes
of a primitive have the same count; indices are unsigned scalars below the vertex count, never the restart value, a
multiple of 3 for triangles; each node has at most one parent, no cycles; scene nodes are roots; node matrices end in
0 0 0 1; image files named by a uri exist. Prints one line per file and a total; exit status 1 when any file fails.
Standard library only (the data is read with the array module).
"""
import array
import json
import math
import os
import sys

COMP = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT2': 4, 'MAT3': 9, 'MAT4': 16}


def check(path):
    errs = []
    stats = {'nodes': 0, 'meshes': 0, 'primitives': 0, 'vertices': 0, 'triangles': 0}

    def err(m):
        if len(errs) < 30:
            errs.append(m)

    try:
        with open(path, 'rb') as f:
            doc = json.loads(f.read().decode('utf-8'))
    except (OSError, ValueError) as e:
        return [f'cannot read: {e}'], stats
    base = os.path.dirname(path)

    def arr(k):
        v = doc.get(k, [])
        return v if isinstance(v, list) else []

    def ok_index(v, k):
        return isinstance(v, int) and not isinstance(v, bool) and 0 <= v < len(arr(k))

    if doc.get('asset', {}).get('version') != '2.0':
        err('asset.version is not "2.0"')
    bufs = []
    for i, b in enumerate(arr('buffers')):
        uri = b.get('uri')
        data = b''
        if uri is None:
            err(f'buffer {i}: no uri (GLB is not checked)')
        else:
            try:
                with open(os.path.join(base, uri), 'rb') as f:
                    data = f.read()
            except OSError:
                err(f'buffer {i}: cannot read {uri}')
        n = b.get('byteLength', 0)
        if n > len(data):
            err(f'buffer {i}: byteLength {n} > the file\'s {len(data)} bytes')
        bufs.append(data[:n])
    views = []
    for i, v in enumerate(arr('bufferViews')):
        b = v.get('buffer')
        off, n, stride = v.get('byteOffset', 0), v.get('byteLength', -1), v.get('byteStride', 0)
        if not ok_index(b, 'buffers'):
            err(f'bufferView {i}: bad buffer')
            views.append(None)
            continue
        if off < 0 or n < 1 or off + n > len(bufs[b]):
            err(f'bufferView {i}: outside its buffer')
            views.append(None)
            continue
        if stride and (stride < 4 or stride > 252 or stride % 4):
            err(f'bufferView {i}: bad byteStride {stride}')
        views.append((b, off, n, stride))
    acc_data = {}

    def accessor(i):
        """the accessor's elements as a flat list (None when broken)"""
        if i in acc_data:
            return acc_data[i]
        a = arr('accessors')[i]
        res = None
        ct, ty, cnt = a.get('componentType'), a.get('type'), a.get('count', 0)
        bv = a.get('bufferView')
        if ct not in COMP or ty not in NCOMP or not isinstance(cnt, int) or cnt < 1:
            err(f'accessor {i}: bad componentType, type or count')
        elif not ok_index(bv, 'bufferViews') or views[bv] is None:
            err(f'accessor {i}: no usable bufferView')
        else:
            code, size = COMP[ct]
            nc = NCOMP[ty]
            b, voff, vlen, stride = views[bv]
            aoff = a.get('byteOffset', 0)
            esz = size * nc
            st = stride or esz
            if aoff % size or (voff + aoff) % size:
                err(f'accessor {i}: misaligned')
            elif aoff + st * (cnt - 1) + esz > vlen:
                err(f'accessor {i}: outside its bufferView')
            else:
                raw = bufs[b][voff + aoff: voff + aoff + st * (cnt - 1) + esz]
                if st == esz:
                    res = array.array(code)
                    res.frombytes(raw[:esz * cnt])
                    if sys.byteorder != 'little':
                        res.byteswap()
                    res = res.tolist()
                else:
                    res = []
                    for k in range(cnt):
                        e = array.array(code)
                        e.frombytes(raw[k * st: k * st + esz])
                        res.extend(e.tolist())
                mn, mx = a.get('min'), a.get('max')
                if (mn is not None and len(mn) != nc) or (mx is not None and len(mx) != nc):
                    err(f'accessor {i}: min / max length')
                if ct == 5126 and any(math.isnan(x) or math.isinf(x) for x in res):
                    err(f'accessor {i}: NaN or infinity')
                if mn is not None and mx is not None and len(mn) == nc and len(mx) == nc:
                    for q in range(nc):
                        col = res[q::nc]
                        lo, hi = min(col), max(col)
                        if ct == 5126:
                            f32 = lambda x: array.array('f', [x])[0]
                            if f32(mn[q]) != lo or f32(mx[q]) != hi:
                                err(f'accessor {i}: min / max do not match the data (component {q})')
                                break
                        elif mn[q] != lo or mx[q] != hi:
                            err(f'accessor {i}: min / max do not match the data (component {q})')
                            break
        acc_data[i] = res
        return res

    for i, im in enumerate(arr('images')):
        if 'uri' in im:
            if not os.path.isfile(os.path.join(base, im['uri'])):
                err(f'image {i}: {im["uri"]} not found')
        elif not ok_index(im.get('bufferView'), 'bufferViews'):
            err(f'image {i}: no uri or bufferView')
    for i, t in enumerate(arr('textures')):
        if not ok_index(t.get('source'), 'images'):
            err(f'texture {i}: bad source')
        if 'sampler' in t and not ok_index(t['sampler'], 'samplers'):
            err(f'texture {i}: bad sampler')
    for i, m in enumerate(arr('materials')):
        pbr = m.get('pbrMetallicRoughness', {})
        if 'baseColorTexture' in pbr and not ok_index(pbr['baseColorTexture'].get('index'), 'textures'):
            err(f'material {i}: bad base colour texture')
        if 'baseColorFactor' in pbr and len(pbr['baseColorFactor']) != 4:
            err(f'material {i}: baseColorFactor is not 4 numbers')
        if m.get('alphaMode', 'OPAQUE') not in ('OPAQUE', 'MASK', 'BLEND'):
            err(f'material {i}: bad alphaMode')
    nvariants = len(doc.get('extensions', {}).get('KHR_materials_variants', {}).get('variants', []))
    for i, mesh in enumerate(arr('meshes')):
        prims = mesh.get('primitives', [])
        if not prims:
            err(f'mesh {i}: no primitives')
        for p, pr in enumerate(prims):
            name = f'mesh {i} primitive {p}'
            stats['primitives'] += 1
            at = pr.get('attributes', {})
            if 'POSITION' not in at:
                err(f'{name}: no POSITION')
                continue
            counts = {}
            for k, ai in at.items():
                if not ok_index(ai, 'accessors'):
                    err(f'{name}: attribute {k} is not an accessor')
                    continue
                a = arr('accessors')[ai]
                d = accessor(ai)
                if d is None:
                    continue
                counts[k] = a['count']
                bv = views[a['bufferView']]
                if (bv[1] + a.get('byteOffset', 0)) % 4 or bv[3] % 4:
                    err(f'{name}: attribute {k} not 4-byte aligned')
                if k == 'POSITION' and (a['componentType'] != 5126 or a['type'] != 'VEC3' or 'min' not in a or 'max' not in a):
                    err(f'{name}: POSITION must be VEC3 float with min / max')
                if k == 'NORMAL' and a['componentType'] == 5126 and a['type'] == 'VEC3':
                    for q in range(0, len(d), 3):
                        if abs(math.sqrt(d[q] ** 2 + d[q + 1] ** 2 + d[q + 2] ** 2) - 1) > 0.005:
                            err(f'{name}: NORMAL not unit length')
                            break
            nv = counts.get('POSITION', 0)
            if any(c != nv for c in counts.values()):
                err(f'{name}: attribute counts differ')
            stats['vertices'] += nv
            mode = pr.get('mode', 4)
            if 'indices' in pr:
                ii = pr['indices']
                if not ok_index(ii, 'accessors') or accessor(ii) is None:
                    err(f'{name}: bad indices')
                    continue
                a = arr('accessors')[ii]
                d = accessor(ii)
                if a['type'] != 'SCALAR' or a['componentType'] not in (5121, 5123, 5125):
                    err(f'{name}: indices are not unsigned scalars')
                restart = {5121: 255, 5123: 65535, 5125: 4294967295}.get(a['componentType'])
                if d and (max(d) >= nv or restart in d):
                    err(f'{name}: index out of range ({max(d)} for {nv} vertices)')
                if mode == 4:
                    if len(d) % 3:
                        err(f'{name}: index count not a multiple of 3')
                    stats['triangles'] += len(d) // 3
            elif mode == 4:
                stats['triangles'] += nv // 3
            if 'material' in pr and not ok_index(pr['material'], 'materials'):
                err(f'{name}: bad material')
            for mp in pr.get('extensions', {}).get('KHR_materials_variants', {}).get('mappings', []):
                if not ok_index(mp.get('material'), 'materials'):
                    err(f'{name}: variant mapping with a bad material')
                if any(not (0 <= v < nvariants) for v in mp.get('variants', [])):
                    err(f'{name}: variant mapping with a bad variant')
    nodes = arr('nodes')
    parent = [-1] * len(nodes)
    for i, n in enumerate(nodes):
        if 'mesh' in n and not ok_index(n['mesh'], 'meshes'):
            err(f'node {i}: bad mesh')
        if 'matrix' in n:
            m = n['matrix']
            if len(m) != 16 or any(not isinstance(x, (int, float)) or math.isnan(x) or math.isinf(x) for x in m):
                err(f'node {i}: bad matrix')
            elif (m[3], m[7], m[11], m[15]) != (0, 0, 0, 1):
                err(f'node {i}: matrix last row is not 0 0 0 1')
            if any(k in n for k in ('translation', 'rotation', 'scale')):
                err(f'node {i}: matrix and TRS together')
        for c in n.get('children', []):
            if not ok_index(c, 'nodes'):
                err(f'node {i}: bad child')
            elif parent[c] >= 0 or c == i:
                err(f'node {c}: more than one parent')
            else:
                parent[c] = i
    for i in range(len(nodes)):
        p, steps = parent[i], 0
        while p >= 0 and steps <= len(nodes):
            p, steps = parent[p], steps + 1
        if steps > len(nodes):
            err(f'node {i}: in a cycle')
            break
    for s, sc in enumerate(arr('scenes')):
        for r in sc.get('nodes', []):
            if not ok_index(r, 'nodes'):
                err(f'scene {s}: bad node')
            elif parent[r] >= 0:
                err(f'scene {s}: node {r} is not a root')
    if 'scene' in doc and not ok_index(doc['scene'], 'scenes'):
        err('bad default scene')
    stats['nodes'] = len(nodes)
    stats['meshes'] = len(arr('meshes'))
    return errs, stats


def main(argv):
    quiet = '--quiet' in argv
    files = []
    for a in argv:
        if a.startswith('--'):
            continue
        if os.path.isdir(a):
            for root, _, names in os.walk(a):
                files += [os.path.join(root, n) for n in names if n.endswith('.gltf')]
        else:
            files.append(a)
    if not files:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    bad = 0
    tot = {'nodes': 0, 'meshes': 0, 'primitives': 0, 'vertices': 0, 'triangles': 0}
    for f in sorted(files):
        errs, st = check(f)
        for k in tot:
            tot[k] += st[k]
        if errs:
            bad += 1
            print(f'{f}: FAIL')
            for e in errs:
                print(f'  {e}')
        elif not quiet:
            print(f'{f}: ok ({st["nodes"]} nodes, {st["meshes"]} meshes, {st["primitives"]} primitives, '
                  f'{st["vertices"]} vertices, {st["triangles"]} triangles)')
    print(f'gltf_check: {len(files) - bad} of {len(files)} files valid; {tot["nodes"]} nodes, {tot["meshes"]} meshes, '
          f'{tot["primitives"]} primitives, {tot["vertices"]} vertices, {tot["triangles"]} triangles')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
