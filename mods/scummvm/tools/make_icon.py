"""Build the ScummVM app tile: official ScummVM icon + the Zune logo (from zune.svg) as a badge."""
import re
from PIL import Image, ImageDraw, ImageChops

SVG = open("zune.svg").read()
D = re.search(r'<path d="(M129[^"]+)"', SVG).group(1)          # the logo path (the first path is an empty frame)
NUM = re.compile(r'-?(?:\d+\.?\d*|\.\d+)')
TOK = re.compile(r'[MmLlHhVvCcSsZz]|-?(?:\d+\.?\d*|\.\d+)')

def flatten(d):
    toks = TOK.findall(d); i = 0; subs = []; cur = None; x = y = sx = sy = 0.0; cmd = None; lc = None
    def num():
        nonlocal i; v = float(toks[i]); i += 1; return v
    while i < len(toks):
        if re.fullmatch(r'[A-Za-z]', toks[i]): cmd = toks[i]; i += 1
        if cmd in 'Zz':
            if cur: subs.append(cur); cur = None
            x, y = sx, sy; continue
        if cmd in 'Mm':
            nx, ny = num(), num()
            if cmd == 'm': nx += x; ny += y
            if cur: subs.append(cur)
            x, y = nx, ny; sx, sy = x, y; cur = [(x, y)]
            cmd = 'l' if cmd == 'm' else 'L'                    # extra pairs after M are lineto
        elif cmd in 'Ll':
            nx, ny = num(), num()
            if cmd == 'l': nx += x; ny += y
            x, y = nx, ny; cur.append((x, y))
        elif cmd in 'Hh':
            nx = num(); x = x + nx if cmd == 'h' else nx; cur.append((x, y))
        elif cmd in 'Vv':
            ny = num(); y = y + ny if cmd == 'v' else ny; cur.append((x, y))
        elif cmd in 'CcSs':
            if cmd in 'Ss':                                      # smooth: first control point = reflection of the last one
                q = [num() for _ in range(4)]
                if cmd == 's': q = [q[0]+x, q[1]+y, q[2]+x, q[3]+y]
                r1 = (2*x - lc[0], 2*y - lc[1]) if lc else (x, y)
                p = [r1[0], r1[1]] + q
            else:
                p = [num() for _ in range(6)]
                if cmd == 'c': p = [p[0]+x, p[1]+y, p[2]+x, p[3]+y, p[4]+x, p[5]+y]
            lc = (p[2], p[3])
            x0, y0 = x, y
            for s in range(1, 21):
                t = s / 20; u = 1 - t
                cur.append((u**3*x0 + 3*u*u*t*p[0] + 3*u*t*t*p[2] + t**3*p[4],
                            u**3*y0 + 3*u*u*t*p[1] + 3*u*t*t*p[3] + t**3*p[5]))
            x, y = p[4], p[5]
    if cur: subs.append(cur)
    return subs

def logo_rgba(px):
    """Zune logo rendered at px x px (transparent background), even-odd fill + the SVG's vertical gradient."""
    K = 4; S = px * K; sc = S / 150.0
    mask = Image.new("1", (S, S), 0)
    for sub in flatten(D):
        m = Image.new("1", (S, S), 0)
        ImageDraw.Draw(m).polygon([(a*sc, b*sc) for a, b in sub], fill=1)
        mask = ImageChops.logical_xor(mask, m)                   # inner subpaths cut holes
    mask = mask.convert("L").resize((px, px), Image.LANCZOS)
    stops = [(0, (0xee,0x90,0x37)), (.0996, (0xee,0x8e,0x3e)), (.259, (0xee,0x88,0x4f)), (.4583, (0xec,0x7d,0x63)),
             (.6897, (0xea,0x6c,0x76)), (.9452, (0xe6,0x55,0x8a)), (1, (0xe6,0x4e,0x8d))]
    y1, y2 = 139.5481 - 1.5, 11.2096 - 1.5                       # gradient vector incl. translate(79,-1.5)
    grad = Image.new("RGB", (px, px))
    for row in range(px):
        yy = (row + .5) / px * 150
        t = min(1, max(0, (yy - y1) / (y2 - y1)))
        for k in range(len(stops) - 1):
            if stops[k][0] <= t <= stops[k+1][0]:
                f = (t - stops[k][0]) / (stops[k+1][0] - stops[k][0]); a, b = stops[k][1], stops[k+1][1]
                c = tuple(int(a[j] + (b[j] - a[j]) * f) for j in range(3)); break
        ImageDraw.Draw(grad).line([(0, row), (px, row)], fill=c)
    out = grad.convert("RGBA"); out.putalpha(mask); return out

ico = Image.open("scummvm.ico"); ico.size = (256, 256); BASE = ico.convert("RGBA")

def tile(S):
    K = 8; W = S * K
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    img.alpha_composite(BASE.resize((int(W*0.95), int(W*0.95)), Image.LANCZOS), (int(W*0.025), int(W*0.025)))
    # round dark badge in the bottom-right corner holding the Zune logo
    r = W * 0.255; cx, cy = W * 0.715, W * 0.715
    d = ImageDraw.Draw(img)
    d.ellipse([cx-r-W*0.02, cy-r-W*0.02, cx+r+W*0.02, cy+r+W*0.02], fill=(255, 255, 255, 255))   # white ring
    d.ellipse([cx-r, cy-r, cx+r, cy+r], fill=(22, 22, 26, 255))
    L = int(r * 1.62); img.alpha_composite(logo_rgba(L), (int(cx - L/2), int(cy - L/2)))
    return img.resize((S, S), Image.LANCZOS)

tile(64).convert("RGB").save("GameThumbnail_zune.png")
tile(256).save("preview_256.png")
print("ok")
