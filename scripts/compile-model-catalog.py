#!/usr/bin/env python3
"""Compile reviewed facts into a bounded, checksummed CBOR/zlib catalog.

No network or XLSX dependency in production builds. The Unicode normalization
table only accepts characters whose NFKC result is a supported ASCII ID symbol;
all other characters remain unmatched (no confusable-character guessing).
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib


def cbor(value):
    def head(kind, n):
        if n < 24:
            return bytes([(kind << 5) | n])
        for marker, size in [(24, 1), (25, 2), (26, 4), (27, 8)]:
            if n < 1 << (size * 8):
                return bytes([(kind << 5) | marker]) + n.to_bytes(size, "big")
        raise ValueError("integer too large")
    if value is None:
        return b"\xf6"
    if isinstance(value, bool):
        return b"\xf5" if value else b"\xf4"
    if isinstance(value, int) and value >= 0:
        return head(0, value)
    if isinstance(value, str):
        raw = value.encode()
        return head(3, len(raw)) + raw
    if isinstance(value, list):
        return head(4, len(value)) + b"".join(cbor(v) for v in value)
    if isinstance(value, dict):
        return head(5, len(value)) + b"".join(cbor(k) + cbor(value[k]) for k in sorted(value))
    raise ValueError(f"unsupported value: {value!r}")


def validate(data):
    rules = data["rules"]
    ids = {r["id"] for r in rules}
    if len(ids) != len(rules) or len(rules) > 2048:
        raise ValueError("duplicate or excessive rules")
    by_id = {r["id"]: r for r in rules}
    for r in rules:
        chain = set()
        node = r
        while node:
            if node["id"] in chain or len(chain) > 16:
                raise ValueError("cyclic/deep inheritance")
            chain.add(node["id"])
            parent = node.get("parent")
            if parent and parent not in ids:
                raise ValueError("missing parent")
            node = by_id.get(parent)
        if r.get("pattern"):
            re.compile(r["pattern"])
            if not r["pattern"].endswith("$") or len(r["pattern"]) > 256:
                raise ValueError("unbounded/non-full pattern")
        if any(k in r.get("facts", {}) for k in ("max_output_tokens", "default_output_tokens")):
            raise ValueError("output limits must not be auto-filled")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    raw = args.source.read_bytes()
    data = json.loads(raw)
    validate(data)
    lock = args.source.with_name("sources.lock.json")
    if lock.exists():
        expected = json.loads(lock.read_text()).get("files", {})
        for name in ("catalog.json", "source.json", "normalization.json"):
            if expected.get(name) != hashlib.sha256(args.source.with_name(name).read_bytes()).hexdigest():
                raise ValueError(f"reviewed source hash mismatch: {name}")
    # Versioned, checked-in NFKC/ASCII projection: independent of the build
    # machine's Python Unicode release. Non-ASCII results cannot match rules.
    normalization = json.loads(args.source.with_name("normalization.json").read_text())
    data.update(normalization)
    data["source_digest"] = hashlib.sha256(raw).hexdigest()
    payload = cbor(data)
    if len(payload) > 2 * 1024 * 1024:
        raise ValueError("catalog too large")
    packed = zlib.compress(payload, 9)
    blob = b"PCMC" + struct.pack("<III", 1, len(payload), zlib.crc32(payload)) + packed
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.with_suffix(".bin").write_bytes(blob)
    args.output.write_text("// Generated; edit native/model-catalog/catalog.json instead.\n"
        "static const unsigned char CatalogBytes[] = {\n" +
        ",\n".join(",".join(str(b) for b in blob[i:i+24]) for i in range(0, len(blob), 24)) + "\n};\n")
    print(f"Model catalog: {len(data['rules'])} nodes, {len(blob)} packed bytes, {len(payload)} decoded bytes")


if __name__ == "__main__":
    main()
