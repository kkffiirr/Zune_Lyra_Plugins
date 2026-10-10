"""Builds scumm_detect.json (filename patterns + md5 table + titles + steam executables) from ScummVM's SCUMM engine sources.
Only entries compiled into our build (SCUMM v0-v8, no Humongous Entertainment games) are kept."""
import json, re, sys
src = sys.argv[1]; out = sys.argv[2]
BS = chr(92)

def active_lines(text, defined):
    """Very small preprocessor: honours #ifdef/#ifndef/#else/#endif for the macros in `defined`."""
    stack, keep = [], []
    for line in text.split(chr(10)):
        t = line.strip()
        if t.startswith("#ifdef") or t.startswith("#ifndef") or t.startswith("#if "):
            m = t.split()
            name = m[1] if len(m) > 1 else ""
            cond = (name in defined) if t.startswith("#ifdef") else (name not in defined) if t.startswith("#ifndef") else False
            stack.append(cond)
            continue
        if t.startswith("#else"):
            if stack: stack[-1] = not stack[-1]
            continue
        if t.startswith("#endif"):
            if stack: stack.pop()
            continue
        if all(stack):
            keep.append(line)
    return chr(10).join(keep)

DEF = {"ENABLE_SCUMM_7_8"}                    # HE (Humongous) is not built
tables = open(src + "/detection_tables.h", encoding="utf-8", errors="replace").read()
det = open(src + "/detection.cpp", encoding="utf-8", errors="replace").read()
md5h = open(src + "/scumm-md5.h", encoding="utf-8", errors="replace").read()

act = active_lines(tables, DEF)
# gameFilenamesTable
start = act.index("gameFilenamesTable[]")
block = act[start:act.index("{ 0, 0, kGenUnchanged", start)] if "{ 0, 0, kGenUnchanged" in act[start:] else act[start:]
pat = re.compile(r'\{\s*"(\w+)"\s*,\s*"([^"]*)"\s*,\s*(kGen\w+)\s*,\s*([\w:]+)\s*,\s*([\w:]+)\s*,\s*(0|"[^"]*")\s*\}')
patterns = [[m.group(1), m.group(2), m.group(3), m.group(5).replace("Common::", ""), m.group(6).strip('"') if m.group(6) != "0" else ""] for m in pat.finditer(block)]
ids = {p[0] for p in patterns}

# titles: PlainGameDescriptor table
tstart = act.index("gameDescriptions[]") if "gameDescriptions[]" in act else act.index("scumm_games[]")
titles = {}
for m in re.finditer(r'\{\s*"(\w+)"\s*,\s*"([^"]*)"\s*\}', act[tstart:]):
    if m.group(1) in ids: titles.setdefault(m.group(1), m.group(2))

# md5 table (keep only ids we build)
md5 = []
for m in re.finditer(r'\{\s*"([0-9a-f]{32})"\s*,\s*"(\w+)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*(-?\d+)\s*,\s*([\w:]+)\s*,\s*([\w:]+)\s*\}', md5h):
    if m.group(2) in ids and not m.group(3).startswith("HE"):
        md5.append([m.group(1), m.group(2), m.group(3), m.group(4), int(m.group(5)), m.group(6).replace("Common::", ""), m.group(7).replace("Common::", "")])

# steam executables: (pattern, platform) -> executable name
steam = {}
sact = active_lines(det, DEF)
for m in re.finditer(r'\{\s*GID_\w+\s*,\s*Common::(kPlatform\w+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"', sact):
    steam[m.group(2).lower() + "|" + m.group(1)] = m.group(4)

json.dump(dict(patterns=patterns, titles=titles, md5=md5, steam=steam), open(out, "w"), separators=(",", ":"))
print("patterns", len(patterns), "ids", len(ids), "titles", len(titles), "md5", len(md5), "steam", len(steam))
print(sorted(ids))
