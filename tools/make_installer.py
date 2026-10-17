#!/usr/bin/env python3
"""Builds the one-file USB installer page for a firmware build.

  python3 tools/make_installer.py build            # writes build/vellum-install-<version>.html
  python3 tools/make_installer.py out -o page.html

The page (tools/installer/install.html) talks to the reader from Chrome or
Edge over Web Serial. Everything it needs goes inside the one file: the
firmware images (gzip, then base64) and esptool-js, so it works offline and
from a downloaded file.

esptool-js comes from the npm registry at a pinned version and is checked
against the registry's sha512 before use. Pass --esptool-tgz to use a copy
already on disk.
"""
import argparse
import base64
import gzip
import hashlib
import io
import json
import pathlib
import re
import struct
import sys
import tarfile
import urllib.request

ESPTOOL_VERSION = "0.7.0"
ESPTOOL_URL = f"https://registry.npmjs.org/esptool-js/-/esptool-js-{ESPTOOL_VERSION}.tgz"
ESPTOOL_SHA512 = "9wHuW/s9t4Vs6HKcr3pra3gbRVWwaN0eabMWodWH4KEJHEYZxgmWwZ3s0F4xcWVCZOc/L98bGZ4uWoZvR8KBKA=="

root = pathlib.Path(__file__).resolve().parent.parent
template = root / "tools/installer/install.html"


def esptool_bundle(tgz_path):
    """esptool-js's ES module bundle, turned into a script that sets window.esptool."""
    data = pathlib.Path(tgz_path).read_bytes() if tgz_path else urllib.request.urlopen(ESPTOOL_URL).read()
    if base64.b64encode(hashlib.sha512(data).digest()).decode() != ESPTOOL_SHA512:
        raise SystemExit("esptool-js download does not match its pinned checksum")
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        src = tar.extractfile("package/bundle.js").read().decode()
    # The bundle has no imports and ends with one export list; make that the
    # return value of a function so the page can use it as a plain script.
    m = re.search(r"export\s*\{([^}]*)\};?\s*$", src)
    if not m or "</script" in src.lower():
        raise SystemExit("esptool-js bundle is not in the expected shape")
    pairs = []
    for item in m.group(1).split(","):
        local, _, public = item.strip().partition(" as ")
        pairs.append(f"{public or local}:{local}")
    return "window.esptool=(()=>{" + src[: m.start()] + "\nreturn{" + ",".join(pairs) + "};})();"


def partitions(table):
    """(label, type, subtype, offset, size) for each entry of a binary partition table."""
    out = []
    for i in range(0, len(table), 32):
        magic, ptype, sub, offset, size, label, _ = struct.unpack("<HBBII16sI", table[i : i + 32])
        if magic != 0x50AA:
            break
        out.append((label.rstrip(b"\0").decode(), ptype, sub, offset, size))
    return out


def app_version(app):
    """The version string in the app's description block (esp_app_desc_t)."""
    magic, = struct.unpack_from("<I", app, 32)
    if app[0] != 0xE9 or magic != 0xABCD5432:
        raise SystemExit("the app image has no app description")
    return app[48:80].split(b"\0")[0].decode()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", help="firmware build folder (has flasher_args.json)")
    ap.add_argument("-o", "--out", help="output file (default: <build>/vellum-install-<version>.html)")
    ap.add_argument("--esptool-tgz", help=f"esptool-js-{ESPTOOL_VERSION}.tgz already on disk")
    args = ap.parse_args()

    build = pathlib.Path(args.build)
    flasher = json.loads((build / "flasher_args.json").read_text())
    if flasher["extra_esptool_args"]["chip"] != "esp32s3":
        raise SystemExit("not an ESP32-S3 build")

    images = []
    names = {"bootloader": "bootloader", "partition-table": "partitions", "otadata": "otadata", "app": "app"}
    for key, name in names.items():
        entry = flasher[key]
        data = (build / entry["file"]).read_bytes()
        images.append({"name": name, "offset": int(entry["offset"], 16), "data": data})
    extra = set(int(o, 16) for o in flasher["flash_files"]) - set(i["offset"] for i in images)
    if extra:
        raise SystemExit(f"flash_files has images this script does not know: {sorted(hex(o) for o in extra)}")

    # The stock firmware keeps its own settings where Vellum's NVS goes, so
    # the page wipes that area when the reader's partition table differs.
    table = next(i["data"] for i in images if i["name"] == "partitions")
    nvs = next(p for p in partitions(table) if p[1] == 1 and p[2] == 2)
    erase = [{"name": nvs[0], "offset": nvs[3], "size": nvs[4]}]

    app = next(i["data"] for i in images if i["name"] == "app")
    version = app_version(app)
    out = pathlib.Path(args.out) if args.out else build / f"vellum-install-{version}.html"

    firmware = {
        "version": version,
        "flashSize": flasher["flash_settings"]["flash_size"],
        "images": [
            {
                "name": i["name"],
                "offset": i["offset"],
                "size": len(i["data"]),
                "md5": hashlib.md5(i["data"]).hexdigest(),
                "gz": base64.b64encode(gzip.compress(i["data"], 9, mtime=0)).decode(),
            }
            for i in images
        ],
        "erase": erase,
    }

    page = template.read_text(encoding="utf-8")
    for marker in ("/*@ESPTOOL@*/", "/*@FIRMWARE@*/null", "@VERSION@"):
        if marker not in page:
            raise SystemExit(f"template is missing {marker}")
    page = page.replace("@VERSION@", version)
    page = page.replace("/*@FIRMWARE@*/null", json.dumps(firmware, separators=(",", ":")))
    page = page.replace("/*@ESPTOOL@*/", esptool_bundle(args.esptool_tgz))
    out.write_text(page, encoding="utf-8")

    for i in images:
        print(f"  0x{i['offset']:06x}  {i['name']:<11} {len(i['data']):>9} bytes")
    for e in erase:
        print(f"  0x{e['offset']:06x}  {e['name']:<11} {e['size']:>9} bytes erased")
    print(f"wrote {out} (Vellum {version}, {out.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    sys.exit(main())
