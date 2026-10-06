"""Build the Hebrew-extended Zegoe UI fonts used by the hebrew-font mod.

Personal use only: the output is derived from Microsoft fonts (Zegoe UI from the Zune firmware, Hebrew glyphs
from your own Windows Segoe UI). Do not commit or redistribute the generated .ttf files.

usage: python tools/make-fonts.py <EXT.bin> [--segoe C:\\Windows\\Fonts]
  EXT.bin  the data image inside the Zune HD 4.5 firmware package (PavoBaseline.cab, unpack with 7-zip)
Output:    mods/hebrew-font/*.ttf (8 files) and mods/fontprobe/ZegoeHebTest.ttf
Requires:  pip install fonttools
"""
import argparse, io, struct
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.recordingPen import DecomposingRecordingPen

ROOT = Path(__file__).resolve().parents[1]
# Zegoe family name -> (output file, Windows font that supplies the Hebrew glyphs)
WEIGHTS = {
    "Zegoe UI":           ("ZegoeUI.ttf",     "segoeui.ttf"),
    "Zegoe UI Light":     ("ZegoeUI_L.ttf",   "segoeuil.ttf"),
    "Zegoe UI SemiLight": ("ZegoeUI_SL.ttf",  "segoeuisl.ttf"),
    "Zegoe UI Semibold":  ("ZegoeUI_SB.ttf",  "seguisb.ttf"),
    "Zegoe UI Bold":      ("ZegoeUI_B.ttf",   "segoeuib.ttf"),
    "Zegoe UI Black":     ("ZegoeUI_Blk.ttf", "segoeuib.ttf"),   # Segoe UI Black has no Hebrew; use Bold's
}
# Unicode ranges copied per script, and one sample letter used for the coverage report.
# Segoe UI covers Hebrew, Arabic, Cyrillic and Greek; other scripts need another source font (see docs/OTHER-LANGUAGES.md).
SCRIPTS = {
    "hebrew":   ([(0x5B0, 0x5C7), (0x5D0, 0x5EA), (0x5F0, 0x5F4), (0x20AA, 0x20AA), (0x200E, 0x200F)], range(0x5D0, 0x5EB)),
    "arabic":   ([(0x600, 0x6FF), (0x750, 0x77F), (0xFB50, 0xFDFF), (0xFE70, 0xFEFF), (0x200C, 0x200F)], range(0x621, 0x64B)),
    "cyrillic": ([(0x400, 0x4FF)], range(0x410, 0x450)),
    "greek":    ([(0x370, 0x3FF)], range(0x391, 0x3AA)),
}

def carve_zegoe(ext):
    """Find intact TrueType fonts in the firmware's data image and return {family: bytes} for the Zegoe UI family."""
    b = ext.read_bytes(); pos = 0; found = {}
    while True:
        i = b.find(b"\x00\x01\x00\x00", pos)
        if i < 0: break
        pos = i + 1
        n = struct.unpack(">H", b[i + 4:i + 6])[0]
        if not 5 <= n <= 30: continue
        tags, end, ok = [], 0, True
        for k in range(n):
            e = b[i + 12 + 16 * k:i + 28 + 16 * k]
            if len(e) < 16: ok = False; break
            tag, _cs, off, ln = struct.unpack(">4sIII", e)
            if not all(32 <= c < 127 for c in tag) or off > len(b) or ln > 20_000_000: ok = False; break
            tags.append(tag); end = max(end, off + ln)
        if not ok or not {b"cmap", b"glyf", b"head"} <= set(tags): continue
        data = b[i:i + end]
        try:
            f = TTFont(io.BytesIO(data), lazy=True)
            fam = f["name"].getDebugName(4) or f["name"].getDebugName(1)
        except Exception:
            continue
        if fam in WEIGHTS: found[fam] = data
    return found

def add_scripts(zegoe_bytes, segoe_path, ranges):
    z = TTFont(io.BytesIO(zegoe_bytes)); s = TTFont(str(segoe_path))
    sc = z["head"].unitsPerEm / s["head"].unitsPerEm
    scm, zcm = s.getBestCmap(), z.getBestCmap(); gs = s.getGlyphSet()
    glyf, hmtx = z["glyf"], z["hmtx"]; order = list(z.getGlyphOrder()); added = 0
    for lo, hi in ranges:
        for cp in range(lo, hi + 1):
            if cp not in scm or cp in zcm: continue
            sn, name = scm[cp], "uni%04X" % cp
            rec = DecomposingRecordingPen(gs); gs[sn].draw(rec)
            pen = TTGlyphPen(None); rec.replay(TransformPen(pen, (sc, 0, 0, sc, 0, 0)))
            glyf.glyphs[name] = pen.glyph(); order.append(name)
            adv, lsb = s["hmtx"][sn]; hmtx.metrics[name] = (round(adv * sc), round(lsb * sc))
            for t in z["cmap"].tables:
                if t.isUnicode(): t.cmap[cp] = name
            added += 1
    z.setGlyphOrder(order); glyf.glyphOrder = order; z["maxp"].numGlyphs = len(order)
    z["post"].formatType = 3.0     # glyph names are not needed on the device
    return z, added

def renamed(src: TTFont, family: str, version: str):
    for r in src["name"].names:
        if r.nameID in (1, 4, 16): r.string = family
        elif r.nameID == 6: r.string = family.replace(" ", "")
        elif r.nameID == 3: r.string = family + ";" + version

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("ext_bin")
    ap.add_argument("--segoe", default=r"C:\Windows\Fonts", help="folder with Segoe UI (segoeui.ttf, segoeuib.ttf, ...)")
    ap.add_argument("--scripts", default="hebrew", help="comma list of: " + ", ".join(SCRIPTS))
    ap.add_argument("--out", default=str(ROOT / "mods" / "hebrew-font"), help="output folder (default: the hebrew-font mod)")
    a = ap.parse_args()
    scripts = [s.strip() for s in a.scripts.split(",") if s.strip()]
    for s in scripts:
        if s not in SCRIPTS: raise SystemExit("unknown script %r (choose from %s)" % (s, ", ".join(SCRIPTS)))
    ranges = [r for s in scripts for r in SCRIPTS[s][0]]
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    zegoe = carve_zegoe(Path(a.ext_bin))
    missing = [f for f in WEIGHTS if f not in zegoe]
    if missing: raise SystemExit("not found in firmware image: %s" % missing)
    for fam, (fn, seg) in WEIGHTS.items():
        z, n = add_scripts(zegoe[fam], Path(a.segoe) / seg, ranges)
        z.save(out / fn)
        cm = TTFont(out / fn).getBestCmap()
        cov = ", ".join("%s %d/%d" % (s, sum(1 for c in SCRIPTS[s][1] if c in cm), len(SCRIPTS[s][1])) for s in scripts)
        print("%-20s -> %-16s +%d glyphs (%s)" % (fam, fn, n, cov))
    # uniquely named copies (Zegoe UH) used for font linking, and a test font for the fontprobe mod
    for src, dst, fam in (("ZegoeUI.ttf", "ZegoeHeb_R.ttf", "Zegoe UH"), ("ZegoeUI_SL.ttf", "ZegoeHeb_SL.ttf", "Zegoe UH SemiLight")):
        t = TTFont(out / src); renamed(t, fam, "2"); t.save(out / dst); print("wrote", dst, "(family '%s')" % fam)
    probe = ROOT / "mods" / "fontprobe"
    if probe.exists():
        t = TTFont(out / "ZegoeUI.ttf"); renamed(t, "ZegoeHebTest", "1"); t.save(probe / "ZegoeHebTest.ttf"); print("wrote fontprobe/ZegoeHebTest.ttf")

if __name__ == "__main__":
    main()
