# Icono de BeTracks Keys a partir de los logos de la marca (assets/brand): la «B» de BeTracks con el play
# y, debajo, «Keys» en rojo. Escribe assets/icon-1024.png (Mac, Windows) y los tamaños de la web.
#   python3 scripts/make_icon.py
from PIL import Image, ImageDraw
import os

root = os.path.join(os.path.dirname(__file__), "..")
b_src = Image.open(os.path.join(root, "assets/brand/betracks-b.png")).convert("RGBA")
logo = Image.open(os.path.join(root, "assets/brand/betracks-keys-logo.png")).convert("RGBA")

def bbox(img, pred):
    px = img.load(); W, H = img.size; xs = []; ys = []
    for y in range(0, H, 2):
        for x in range(0, W, 2):
            if pred(px[x, y]): xs.append(x); ys.append(y)
    return min(xs), min(ys), max(xs) + 2, max(ys) + 2

# La B: lo azul y lo que encierra (el play es negro y queda dentro del recorte).
b = b_src.crop(bbox(b_src, lambda p: p[2] > 150 and p[0] < 120))
# «keys»: lo rojo del logo.
keys = logo.crop(bbox(logo, lambda p: p[0] > 150 and p[1] < 90 and p[2] < 120))

def render(size):
    S = 1024
    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # Fondo negro redondeado, con el margen de los iconos de macOS.
    bg = Image.new("L", (S, S), 0)
    ImageDraw.Draw(bg).rounded_rectangle((100, 100, 924, 924), radius=185, fill=255)
    canvas.paste((0, 0, 0, 255), (0, 0), bg)
    bh = 440; bw = int(b.width * bh / b.height)
    bimg = b.resize((bw, bh), Image.LANCZOS)
    kw = 400; kh = int(keys.height * kw / keys.width)
    kimg = keys.resize((kw, kh), Image.LANCZOS)
    top = (S - (bh + 40 + kh)) // 2
    canvas.alpha_composite(bimg, ((S - bw) // 2, top))
    canvas.alpha_composite(kimg, ((S - kw) // 2, top + bh + 40))
    return canvas.resize((size, size), Image.LANCZOS)

for size, rel in [(1024, "assets/icon-1024.png"), (512, "website/keys/img/icon.png"),
                  (256, "website/keys/img/icon-256.png"), (64, "website/keys/img/favicon.png")]:
    render(size).save(os.path.join(root, rel))
# Logo ancho para la cabecera de la web.
w = 1400; logo.resize((w, int(logo.height * w / logo.width)), Image.LANCZOS).save(os.path.join(root, "website/keys/img/logo.png"))

# Logo de la cabecera de la app (BinaryData): sin el negro de fondo (alfa = canal más brillante).
hl = logo.convert("RGB").resize((720, int(logo.height * 720 / logo.width)), Image.LANCZOS)
out = Image.new("RGBA", hl.size); s_ = hl.load(); o_ = out.load()
for y in range(hl.height):
    for x in range(hl.width):
        r, g, b = s_[x, y]; a = max(r, g, b)
        o_[x, y] = (0, 0, 0, 0) if a < 8 else (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a)
out.save(os.path.join(root, "assets/brand/header-logo.png"))
print("Iconos y logo listos")
