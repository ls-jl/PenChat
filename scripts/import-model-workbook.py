#!/usr/bin/env python3
"""One-time source import; production builds only read the reviewed JSON files.

Uses the XLSX XML container, not Excel or an installed spreadsheet engine.
Never edits the source workbook. Review changes before compiling the catalog.
"""
import argparse
import hashlib
import json
from pathlib import Path
from zipfile import ZipFile
import xml.etree.ElementTree as ET

NS = {"s": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}


def rows(archive, sheet):
    strings = []
    if "xl/sharedStrings.xml" in archive.namelist():
        for item in ET.fromstring(archive.read("xl/sharedStrings.xml")):
            strings.append("".join(t.text or "" for t in item.findall(".//s:t", NS)))
    result = []
    for row in ET.fromstring(archive.read(f"xl/worksheets/sheet{sheet}.xml")).findall(".//s:row", NS):
        values = {}
        for cell in row:
            col = "".join(c for c in cell.attrib["r"] if c.isalpha())
            value = cell.find("s:v", NS)
            if cell.get("t") == "inlineStr":
                value = "".join(t.text or "" for t in cell.findall(".//s:t", NS))
            elif value is not None:
                value = value.text
                if cell.get("t") == "s":
                    value = strings[int(value)]
                elif cell.get("t") != "str":
                    value = float(value)
                    if value.is_integer():
                        value = int(value)
            else:
                value = None
            values[col] = value
        result.append(values)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workbook", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.resolve() == args.workbook.resolve() or args.output.suffix.lower() != ".json":
        parser.error("output must be a separate JSON file; the workbook is read-only")
    with ZipFile(args.workbook) as archive:
        source = rows(archive, 2)
        start = next(i for i, row in enumerate(source) if row.get("B") == "规则ID")
        headers = source[start]
        raw = [{headers[c]: v for c, v in row.items() if c in headers and headers[c]}
               for row in source[start + 1:] if row.get("B")]
        index = {r["规则ID"]: r for r in raw}
        keep = {r["规则ID"] for r in raw if r.get("匹配模式")}
        for key in list(keep):
            while index[key].get("父规则ID"):
                key = index[key]["父规则ID"]
                keep.add(key)
        test_rows = rows(archive, 3)
        start = next(i for i, row in enumerate(test_rows) if row.get("B") == "测试输入")
        tests = [{"input": r["B"], "rule": r.get("C"),
                  "context": r.get("F"), "modalities": r.get("J")}
                 for r in test_rows[start + 1:] if r.get("B")]
    result = {"format": 1, "source_sha256": hashlib.sha256(args.workbook.read_bytes()).hexdigest(),
              "source_date": "2026-10-25", "original_nodes": len(raw),
              "rules": [r for r in raw if r["规则ID"] in keep], "tests": tests}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
