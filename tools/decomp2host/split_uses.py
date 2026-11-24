#!/usr/bin/env python3
"""split_uses.py -- the split-array census by use (MORE RIVALS): in every generated unit, each loop bounded by the race
car count (D_0064D37C, gWorld +0x12AD1C, GetCarCount...) and each subscript by a car's index (+0x19AC), with the
expression indexed and how it is declared: split (split_array / split_open / flat_array / split_global), decl (a plain
declaration: a candidate unless it is 8 long, a player's row or outside a Single Event Race), nodecl, raw (offset
arithmetic). split_census.py works from the declarations; this finds what they miss (a view through another class,
raw offsets). The loops: bounded by the car count ("cars"), the AI count (the stage record's +0xA4, "ai"), the start
slots (0x0064D360, "slots"), the players (D_0064D3A0, "players"), `!=` ends of those ("ne"), and constant 5 / 6 / 7 / 8 in
a unit that touches the car list or a split array ("const"); the indices: a car's +0x19AC through a temporary ("index")
or in the subscript itself ("direct"), a walk of a pointer over the car list ("walk"), and a function's parameter that
it subscripts with, where a call passes it a car index ("param", the callee's site).
Usage: split_uses.py <gen/native/units> [<gen/native/include>]"""
import collections
import os
import re
import sys

SRC = r"(?:D_0064D37C|0x12AD1C|field_12AD1C|GetCarCount|GetNumRaceCars|GetNumberOfCars|GetNumCars)"
BOUNDS = [("cars", SRC), ("ai", r"(?:D_0051AD00\.field_A4|0x0051ADA4u?|D_0051ADA4)"),
          ("slots", r"(?:D_0064D360|0x0064D360u?)"), ("players", r"(?:D_0064D3A0|0x0064D3A0u?)")]
IDX = r"(?:field_19AC|0x19AC)"
CARLIST = r"(?:D_0064D388|field_12AD28|0x12AD28|0x0064D388)"
SPLIT = r"split_array|split_open|flat_array|split_global|split_local_array|split::"


def functions(text):
    for m in re.finditer(r"^[^\s#/][^;{}]*\)\s*(?:const\s*)?\{\s*$", text, re.M):
        a = j = m.end()
        d = 1
        while j < len(text) and d:
            d += (text[j] == "{") - (text[j] == "}")
            j += 1
        yield " ".join(m.group(0).split("(")[0].split())[-50:], text[a:j]


def subscripts(body, v):
    pats = [r"([\w\.\->\)\]]+)\s*\[\s*%s\s*\]" % v, r"pcast_elem<[^>]*>\(\s*([^,]+?),\s*%s\s*\)" % v,
            r"(\b%s\s*\*\s*(?:0x)?[0-9A-Fa-f]+u?\s*\+\s*0x[0-9A-Fa-f]+)" % v,
            r"(0x[0-9A-Fa-f]+u?\s*\+\s*%s\s*\*\s*(?:0x)?[0-9A-Fa-f]+)" % v]
    hits = collections.OrderedDict()
    for p in pats:
        for m in re.finditer(p, body):
            hits[" ".join(m.group(1).split())] = 1
    return list(hits)


def load(units_dir):
    out = []
    for f in sorted(os.listdir(units_dir)):
        if f.endswith(".cpp"):
            raw = open(os.path.join(units_dir, f), encoding="utf-8", errors="replace").read()
            out.append((f[:-4], re.sub(r"/\*.*?\*/", "", re.sub(r"//[^\n]*", "", raw), flags=re.S)))
    return out


def sites(units_dir):
    units = load(units_dir)
    params = collections.defaultdict(list)   # callee name -> [(caller unit, argument position)]
    for unit, text in units:
        carry = bool(re.search(CARLIST, text) or re.search(r"split_array|split_open|split_global|split::", text))
        for head, body, sig in functions_sig(text):
            vs = set()
            for name, src in BOUNDS:
                cnt = set(re.findall(r"\b(\w+)\s*=\s*[^;=]*" + src, body))
                bound = src + ("|" + "|".join(r"\b%s\b" % re.escape(c) for c in cnt) if cnt else "")
                vs |= {(v, name) for v in re.findall(r"\b(\w+)\s*<=?\s*(?:[^;)]*?)(?:%s)" % bound, body)}
                vs |= {(v, "ne") for v in re.findall(r"\b(\w+)\s*!=\s*(?:%s)\b" % bound, body)}
            if carry:
                vs |= {(v, "const") for v in re.findall(r"\b(\w+)\s*<\s*\(?\s*([5-8])\s*\)?\s*[;)]", body) and
                       [x[0] for x in re.findall(r"\b(\w+)\s*<\s*\(?\s*([5-8])\s*\)?\s*[;)]", body)]}
            vs |= {(v, "index") for v in re.findall(r"\b(\w+)\s*=\s*[^;=]*" + IDX, body)}
            vs |= {(v, "walk") for v in re.findall(r"\b(\w+)\s*=\s*&\s*[^;=]*%s[^;=]*;" % CARLIST, body)
                   if re.search(r"(?:\+\+\s*%s\b|\b%s\s*\+\+|\b%s\s*\+=)" % (v, v, v), body)}
            for v, why in sorted(vs):
                if v in ("field_0", "if", "while", "return") or not re.match(r"^[A-Za-z_]\w*$", v):
                    continue
                for h in subscripts(body, v):
                    yield unit, head, v, why, h, text
                if why == "walk":
                    yield unit, head, v, why, "(a walk of %s over the car list)" % v, text
            for m in re.finditer(r"([\w\.\->\)]+)\s*\[([^\[\]]*%s[^\[\]]*)\]" % IDX, body):
                yield unit, head, "-", "direct", " ".join(m.group(1).split()), text
            # calls passing a car index: (callee, argument position)
            for m in re.finditer(r"\b(\w+)\s*\(([^;{}]*?%s[^;{}]*?)\)\s*[;,)]" % IDX, body):
                args = split_args(m.group(2))
                for k, a in enumerate(args):
                    if re.search(IDX, a) and not re.search(r"\[[^\]]*%s" % IDX, a):
                        params[m.group(1)].append((unit, k))
    # the callees: the parameter at that position, subscripted in the callee's body
    seen = set()
    for unit, text in units:
        for head, body, sig in functions_sig(text):
            name = re.findall(r"(\w+)\s*$", head)
            name = name[0] if name else ""
            short = name.split("::")[-1]
            calls = params.get(name) or params.get(short) or []
            for caller, k in calls:
                ps = split_args(sig)
                if k >= len(ps):
                    continue
                pv = re.findall(r"(\w+)\s*$", ps[k].strip())
                if not pv or (unit, name, k) in seen:
                    continue
                seen.add((unit, name, k))
                for h in subscripts(body, pv[0]):
                    yield unit, head, pv[0], "param (from %s)" % caller[:40], h, text


def split_args(s):
    out, d, cur = [], 0, ""
    for c in s:
        if c in "(<[":
            d += 1
        elif c in ")>]":
            d -= 1
        if c == "," and d == 0:
            out.append(cur)
            cur = ""
        else:
            cur += c
    if cur.strip():
        out.append(cur)
    return out


def functions_sig(text):
    for m in re.finditer(r"^[^\s#/][^;{}]*\)\s*(?:const\s*)?\{\s*$", text, re.M):
        a = j = m.end()
        d = 1
        while j < len(text) and d:
            d += (text[j] == "{") - (text[j] == "}")
            j += 1
        decl = " ".join(m.group(0).split())
        head = decl.split("(")[0]
        sig = decl[len(head) + 1:decl.rindex(")")] if ")" in decl else ""
        yield head[-60:], text[a:j], sig


def main():
    units_dir = sys.argv[1]
    inc = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(units_dir.rstrip("/")), "include")
    headers = []
    for root, _, fs in os.walk(inc):
        headers += [open(os.path.join(root, f), errors="replace").read() for f in fs if f.endswith(".h")]
    headers = "\n".join(headers)
    for unit, fn, v, why, e, text in sites(units_dir):
        if re.search(r"\*\s*(?:0x)?[0-9A-Fa-f]+u?\s*\+\s*0x|0x[0-9A-Fa-f]+u?\s*\+\s*\w+\s*\*", e):
            print("\t".join(["raw", unit, fn, v, why, e]))
            continue
        names = re.findall(r"(field_[0-9A-F]+|\b[A-Za-z_]\w*)", e)
        last = names[-1] if names else e
        decl = re.compile(r"^[^\n;]*\b%s\b\s*(?:\[[^\]]*\])*\s*;[^\n]*" % re.escape(last), re.M)
        ds = decl.findall(text) or decl.findall(headers)
        d = " | ".join(sorted(set(" ".join(x.split())[:100] for x in ds)))
        kind = "split" if re.search(SPLIT, d + " " + e) else "decl" if ds else "nodecl"
        print("\t".join([kind, unit, fn, v, why, "%s  {%s}" % (e, d[:220])]))


if __name__ == "__main__":
    main()
