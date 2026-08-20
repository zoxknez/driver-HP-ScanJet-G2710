#!/usr/bin/env python
"""
Parser biblioteka za hp3900 reference izvor.

Koristi je tools/extract-hp3900-profile.py.

Izvor istine: third_party/hp3900-reference/  (GPL-2.0, Jonathan Bravo Lopez)
Izlaz:        native/core/device/G2710Profile.generated.h

G2710 (HPG2710 = 0x07) je u svakoj konfiguracionoj tabeli identican HP3800, i
svaka switch(dev_model) grana dispecuje na hp3800_* familiju. Zato:
  - tabele koje kljucaju na uredjaj  -> filtriramo red za HPG2710
  - hp3800_* tabele                  -> uzimamo u celosti (vec su G2710-specificne)

"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REF = ROOT / "third_party" / "hp3900-reference"
OUT = ROOT / "native" / "core" / "device" / "G2710Profile.generated.h"

SQUOTE = chr(39)
DQUOTE = chr(34)


# ---------------------------------------------------------------- lexing ----

def strip_comments(text: str) -> str:
    """Uklanja C komentare, cuvajuci prelome redova radi brojanja."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text[i] in (DQUOTE, SQUOTE):
            q = text[i]
            j = i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def parse_defines(text: str) -> dict:
    """Skuplja `#define NAME <int>` u simbolicku tabelu."""
    syms = {}
    for m in re.finditer(r"^\s*#\s*define\s+(\w+)\s+(-?\w+)\s*$", text, re.M):
        try:
            syms[m.group(1)] = int(m.group(2), 0)
        except ValueError:
            pass
    return syms


def parse_enum(text: str, containing: str) -> list:
    """Imena clanova enuma koji sadrzi `containing`, redom deklaracije."""
    i = text.find(containing)
    if i < 0:
        raise KeyError("enum koji sadrzi " + containing + " nije nadjen")
    start = text.rfind("enum", 0, i)
    end = text.find("};", i)
    body = text[text.find("{", start) + 1:end]
    names = []
    for part in body.split(","):
        part = part.split("=")[0].strip()
        if re.fullmatch(r"[A-Za-z_]\w*", part):
            names.append(part)
    return names


# --------------------------------------------------------------- parsing ----

def find_function_body(text: str, name: str) -> str:
    """Telo definicije funkcije (bez spoljnih viticastih zagrada).

    Deklaracije se preskacu jer trazimo `) {`, a ne `);`.
    """
    m = re.search(r"^\s*(?:static\s+)?[\w\s*]+?\b" + re.escape(name) +
                  r"\s*\([^)]*\)\s*\{", text, re.M)
    if not m:
        raise KeyError("funkcija nije nadjena: " + name)
    start = m.end() - 1
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
    raise ValueError("nezatvorena funkcija: " + name)


def find_table_initializer(body: str, array_name=None) -> str:
    """Sadrzaj `= { ... }` prvog (ili imenovanog) niza u telu funkcije."""
    pat = (re.escape(array_name) if array_name else r"\w+") + r"\s*\[\s*\]\s*=\s*\{"
    m = re.search(pat, body)
    if not m:
        raise KeyError("tabela nije nadjena (array=" + str(array_name) + ")")
    start = m.end() - 1
    depth = 0
    for i in range(start, len(body)):
        if body[i] == "{":
            depth += 1
        elif body[i] == "}":
            depth -= 1
            if depth == 0:
                return body[start + 1:i]
    raise ValueError("nezatvorena tabela")


def parse_braces(src: str, syms: dict):
    """C brace-initializer -> ugnjezdene Python liste."""
    pos, n = 0, len(src)

    def scalar(tok):
        tok = tok.strip()
        if not tok:
            return None
        if tok in syms:
            return syms[tok]
        try:
            if "." in tok or "e" in tok.lower():
                return float(tok)
            return int(tok, 0)
        except ValueError:
            return tok

    def group():
        nonlocal pos
        pos += 1
        items, buf = [], []
        while pos < n:
            c = src[pos]
            if c == "{":
                items.append(group())
                buf = []
            elif c == "}":
                pos += 1
                if "".join(buf).strip():
                    items.append(scalar("".join(buf)))
                return items
            elif c == ",":
                if "".join(buf).strip():
                    items.append(scalar("".join(buf)))
                buf = []
                pos += 1
            else:
                buf.append(c)
                pos += 1
        raise ValueError("nezatvorena grupa")

    rows = []
    while pos < n:
        if src[pos] == "{":
            rows.append(group())
        else:
            pos += 1
    if not rows:
        # Flat inicijalizator bez ugnjezdenih zagrada (npr. hp3800_motor steps[]).
        rows = [[scalar(t) for t in src.split(",") if t.strip()]]
    return rows


def parse_switch_map(body: str, syms: dict) -> dict:
    """`case NAME: rst = VALUE;` -> {NAME: value}.

    Podrzava i indeksirani oblik `rst = value[N]` uz lokalni niz.
    """
    arrays = {}
    for m in re.finditer(r"(\w+)\s*\[\s*\]\s*=\s*\{([^}]*)\}", body):
        vals = []
        for x in m.group(2).split(","):
            x = x.strip()
            if x:
                try:
                    vals.append(int(x, 0))
                except ValueError:
                    pass
        arrays[m.group(1)] = vals

    out = {}
    for m in re.finditer(r"case\s+(\w+)\s*:\s*rst\s*=\s*([^;]+);", body):
        name, raw = m.group(1), m.group(2).strip()
        idx = re.fullmatch(r"(\w+)\s*\[\s*(\d+)\s*\]", raw)
        if idx:
            # `int *value = value1;` alias: ako ime nije nadjeno, a postoji
            # tacno jedan lokalni niz, koristi njega.
            src_arr = arrays.get(idx.group(1))
            if src_arr is None and len(arrays) == 1:
                src_arr = next(iter(arrays.values()))
            if src_arr is None:
                out[name] = raw
            else:
                out[name] = src_arr[int(idx.group(2))]
        elif raw in syms:
            out[name] = syms[raw]
        else:
            try:
                out[name] = int(raw, 0)
            except ValueError:
                out[name] = raw
    return out


def decode_motor_curves(stream: list) -> list:
    """Dekodira flat SANE_Int stream iz hp3800_motor().

    Format:  [mri, msi, skiplinecount, motorbackstep]
             pa segmenti [curvetype, curvename, v1..vN, 0]
             -2 = kraj jedne motorcurve, sledi jos jedna
             -1 = kraj svih
    """
    curves, i, n = [], 0, len(stream)
    while i < n and stream[i] != -1:
        head = stream[i:i + 4]
        i += 4
        segments = []
        while i < n and stream[i] not in (-1, -2):
            ctype, cname = stream[i], stream[i + 1]
            i += 2
            vals = []
            while i < n and stream[i] != 0:
                vals.append(stream[i])
                i += 1
            i += 1
            segments.append({"type": ctype, "name": cname, "values": vals})
        curves.append({
            "mri": head[0], "msi": head[1],
            "skiplinecount": head[2], "motorbackstep": head[3],
            "segments": segments,
        })
        if i < n and stream[i] == -2:
            i += 1
    return curves


# ------------------------------------------------------------- rendering ----

def flat(x) -> str:
    """Ugnjezdena lista -> C++ brace initializer."""
    if isinstance(x, list):
        return "{" + ", ".join(flat(v) for v in x) + "}"
    if isinstance(x, float):
        s = repr(x)
        return s if ("." in s or "e" in s) else s + ".0"
    return str(x)


def rows_block(rows, indent="    ") -> str:
    return "\n".join(indent + flat(r) + "," for r in rows)
