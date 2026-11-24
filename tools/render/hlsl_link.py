#!/usr/bin/env python3
"""d3d12 (build_shaders.sh): makes a fragment shader's HLSL input signature the same as its vertex shader's output.

Direct3D 12 links a pipeline's stages by their packed signatures: a pixel shader that reads only some of the vertex
shader's outputs (light.frag reads 3 of mesh.vert's 6) is packed differently and pipeline creation fails
(E_INVALIDARG), where Vulkan matches by location. spirv-cross emits only the inputs a shader reads, so the fragment's
`struct SPIRV_Cross_Input` gets the vertex shader's `struct SPIRV_Cross_Output` members (the same order and semantics,
the system values left out), plus the fragment's own system-value inputs (SV_Position for gl_FragCoord ...). Every input
the fragment had must be among the vertex outputs, with the same name and semantic, or this fails.
Usage: hlsl_link.py <frag.hlsl> <vert.hlsl>   (rewrites frag.hlsl in place)"""
import re
import sys


def struct_body(text, name):
    m = re.search(r"struct %s\s*\{(.*?)\};" % name, text, re.S)
    return m


def members(body):
    out = []
    for line in body.split("\n"):
        line = line.strip()
        if not line:
            continue
        m = re.match(r"(.*?)\s+(\w+)\s*:\s*(\w+)\s*;$", line)
        if not m:
            raise SystemExit("hlsl_link: cannot read the member line %r" % line)
        out.append((m.group(1), m.group(2), m.group(3), line))
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    frag_path, vert_path = argv[1], argv[2]
    frag = open(frag_path).read()
    vert = open(vert_path).read()
    vo = struct_body(vert, "SPIRV_Cross_Output")
    if not vo:
        raise SystemExit("hlsl_link: %s has no SPIRV_Cross_Output" % vert_path)
    outs = [m for m in members(vo.group(1)) if not m[2].upper().startswith("SV_")]
    fi = struct_body(frag, "SPIRV_Cross_Input")
    if not fi:   # a fragment shader that reads nothing: give it the vertex outputs all the same
        ins = []
    else:
        ins = members(fi.group(1))
    by_name = {m[1]: m for m in outs}
    for t, n, sem, _ in ins:
        if sem.upper().startswith("SV_"):
            continue
        o = by_name.get(n)
        if not o or o[2] != sem:
            raise SystemExit("hlsl_link: %s reads %s : %s, which %s does not write" % (frag_path, n, sem, vert_path))
    system = [m for m in ins if m[2].upper().startswith("SV_")]
    body = "\n" + "".join("    %s\n" % m[3] for m in outs + system)
    if fi:
        frag = frag[:fi.start(1)] + body + frag[fi.end(1):]
    else:
        # no input struct: add one and take it in main (its members unused)
        m = re.search(r"\n(\w+)\s+main\(\)", frag)
        if not m:
            raise SystemExit("hlsl_link: %s has neither SPIRV_Cross_Input nor main()" % frag_path)
        frag = frag[:m.start()] + "\nstruct SPIRV_Cross_Input\n{" + body + "};\n" + frag[m.start():]
        frag = re.sub(r"\n(\w+)\s+main\(\)", r"\n\1 main(SPIRV_Cross_Input stage_input)", frag, count=1)
    open(frag_path, "w").write(frag)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
