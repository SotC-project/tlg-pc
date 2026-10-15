# Keyboard and Xbox versions of the controller picture in the game's tutorial hints
# (alert/guide_controller_base_bc7.gnf, 256 x 128 BC7). The game lights buttons by drawing glow
# sprites at the DualShock's button positions, so every label stays where its PS button was.
# Input: the runtime's dump of the texture (tools/4641711da2e67237.png, decoded).
# Output: <profile>/sotc_textures/{keyboard,xbox}/4641711da2e67237.bin, previews in tools/.
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(__file__))
from bc7enc import encode

HERE = os.path.dirname(os.path.abspath(__file__))
HASH = "4641711da2e67237"
PROFILE = sys.argv[1] if len(sys.argv) > 1 else "D:/tlg-pc/testrun/user"

# Button centres in the 256 x 128 picture.
TRIANGLE, SQUARE, CIRCLE, CROSS = (179.5, 59), (167, 70.5), (192.5, 70.5), (179.5, 82)
LSTICK, RSTICK = (99.5, 93), (153.5, 93)
SHARE, OPTIONS, HOME = (92.5, 52), (162, 52), (125.5, 86)
L1, R1 = (75, 27), (180, 27)
TOUCHPAD = (98, 35, 157, 72)

LINE = (200, 200, 200, 255)
BODY = (0, 0, 0, 255)


def font(size):
    for name in ("segoeuib.ttf", "arialbd.ttf"):
        try:
            return ImageFont.truetype("C:/Windows/Fonts/" + name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def text(d, centre, s, size, fill=LINE, face=None):
    f = ImageFont.truetype("C:/Windows/Fonts/" + face, size) if face else font(size)
    d.text(centre, s, font=f, fill=fill, anchor="mm")


def disc(d, centre, r, fill, outline=None):
    x, y = centre
    d.ellipse((x - r, y - r, x + r, y + r), fill=fill, outline=outline)


def base():
    im = Image.open(os.path.join(HERE, HASH + ".png")).convert("RGBA")
    d = ImageDraw.Draw(im)
    # Clear the PlayStation markings: face symbols, touchpad grid, Share/Options, PS logo.
    for c in (TRIANGLE, SQUARE, CIRCLE, CROSS):
        disc(d, c, 6.5, BODY)
    x0, y0, x1, y1 = TOUCHPAD
    d.rounded_rectangle(TOUCHPAD, 3, fill=BODY, outline=LINE)
    for c in (SHARE, OPTIONS):
        d.rectangle((c[0] - 4, c[1] - 6, c[0] + 4, c[1] + 6), fill=BODY)
    d.rectangle((HOME[0] - 8, HOME[1] - 6, HOME[0] + 8, HOME[1] + 6), fill=BODY)
    return im, d


def xbox():
    im, d = base()
    for c, letter, colour in ((TRIANGLE, "Y", (230, 190, 40, 255)), (SQUARE, "X", (60, 130, 230, 255)),
                              (CIRCLE, "B", (220, 60, 50, 255)), (CROSS, "A", (90, 190, 60, 255))):
        disc(d, c, 5.5, BODY, outline=colour)
        text(d, (c[0], c[1] + 0.5), letter, 9, colour)
    # View (two windows) and Menu (three lines).
    x, y = SHARE
    d.rectangle((x - 3, y - 3, x + 1, y + 1), outline=LINE)
    d.rectangle((x - 1, y - 1, x + 3, y + 3), fill=BODY, outline=LINE)
    x, y = OPTIONS
    for dy in (-2, 0, 2):
        d.line((x - 3, y + dy, x + 3, y + dy), fill=LINE)
    disc(d, HOME, 5, BODY, outline=LINE)
    text(d, (HOME[0], HOME[1] + 0.5), "X", 7)
    text(d, L1, "LB", 7)
    text(d, R1, "RB", 7)
    return im


def keyboard():
    im, d = base()
    # Space drawn as its key symbol: the caps are only 12 pixels wide.
    for c in (TRIANGLE, SQUARE, CIRCLE, CROSS):
        d.rounded_rectangle((c[0] - 6, c[1] - 5, c[0] + 6, c[1] + 5), 2, fill=BODY, outline=LINE)
    x, y = TRIANGLE
    d.line([(x - 3.5, y - 1.5), (x - 3.5, y + 1.5), (x + 3.5, y + 1.5), (x + 3.5, y - 1.5)], fill=LINE)
    text(d, (SQUARE[0], SQUARE[1] + 0.5), "F", 8)
    text(d, (CIRCLE[0], CIRCLE[1] + 0.5), "E", 8)
    text(d, (CROSS[0], CROSS[1] + 0.5), "C", 8)
    text(d, (LSTICK[0], LSTICK[1] + 0.5), "WASD", 7)
    # Right stick: a mouse.
    x, y = RSTICK
    d.rounded_rectangle((x - 4, y - 6, x + 4, y + 6), 4, outline=LINE)
    d.line((x, y - 6, x, y - 2), fill=LINE)
    text(d, ((TOUCHPAD[0] + TOUCHPAD[2]) / 2, (TOUCHPAD[1] + TOUCHPAD[3]) / 2), "Tab", 10)
    text(d, (OPTIONS[0] + 3, OPTIONS[1]), "Esc", 6)
    text(d, L1, "Q", 8)
    text(d, R1, "R", 8)
    return im


# ---- The pause menu's Controls guide (sys_option_item_guide_padimage, 1920 x 1080) ----
# The game's labels and leader lines point at the DualShock's buttons, so they stay put.
GUIDE = "9e20a9967275538f"
G_TRIANGLE, G_CIRCLE, G_CROSS, G_SQUARE = (726, 308), (769, 348), (726, 385), (684, 348)
G_SHARE, G_OPTIONS, G_HOME = (429, 269), (665, 269), (546, 405)
G_L1, G_L2, G_R1, G_R2 = (367, 221), (372, 197), (725, 221), (722, 197)
G_LSTICK, G_RSTICK = (456, 433), (639, 433)
G_TOUCHPAD = (452, 246, 642, 359)
G_FACE = (73, 70, 70, 255)
G_BODY = (29, 22, 22, 255)
G_SHOULDER = (76, 73, 73, 255)
G_LINE = (235, 235, 235, 255)


def guide_base():
    im = Image.open(os.path.join(HERE, GUIDE + ".png")).convert("RGBA")
    d = ImageDraw.Draw(im)
    for c in (G_TRIANGLE, G_CIRCLE, G_CROSS, G_SQUARE):
        disc(d, c, 16, G_FACE)
    for c in (G_SHARE, G_OPTIONS):
        d.rectangle((c[0] - 26, c[1] - 7, c[0] + 26, c[1] + 7), fill=G_BODY)
    for c in (G_L1, G_L2, G_R1, G_R2):
        d.rectangle((c[0] - 11, c[1] - 6, c[0] + 11, c[1] + 6), fill=G_SHOULDER)
    disc(d, G_HOME, 14, G_FACE)
    return im, d


def guide_xbox():
    im, d = guide_base()
    for c, letter, colour in ((G_TRIANGLE, "Y", (235, 190, 40, 255)), (G_SQUARE, "X", (70, 140, 235, 255)),
                              (G_CIRCLE, "B", (225, 65, 55, 255)), (G_CROSS, "A", (95, 195, 65, 255))):
        text(d, (c[0], c[1] + 1), letter, 22, colour)
    text(d, G_SHARE, "VIEW", 11, G_LINE)
    text(d, G_OPTIONS, "MENU", 11, G_LINE)
    for c, label in ((G_L1, "LB"), (G_L2, "LT"), (G_R1, "RB"), (G_R2, "RT")):
        text(d, c, label, 10, G_LINE)
    disc(d, G_HOME, 9, None, outline=G_LINE)
    text(d, (G_HOME[0], G_HOME[1] + 1), "X", 11, G_LINE)
    return im


def guide_keyboard():
    im, d = guide_base()
    for c in (G_TRIANGLE, G_CIRCLE, G_CROSS, G_SQUARE):
        d.rounded_rectangle((c[0] - 14, c[1] - 12, c[0] + 14, c[1] + 12), 4, outline=G_LINE, width=2)
    x, y = G_TRIANGLE
    d.line([(x - 8, y - 3), (x - 8, y + 4), (x + 8, y + 4), (x + 8, y - 3)], fill=G_LINE, width=2)
    text(d, (G_CIRCLE[0], G_CIRCLE[1] + 1), "E", 17, G_LINE)
    text(d, (G_CROSS[0], G_CROSS[1] + 1), "C", 17, G_LINE)
    text(d, (G_SQUARE[0], G_SQUARE[1] + 1), "F", 17, G_LINE)
    text(d, G_OPTIONS, "ESC", 11, G_LINE)
    for c, label in ((G_L1, "Q"), (G_L2, "Z"), (G_R1, "R"), (G_R2, "X")):
        text(d, c, label, 11, G_LINE)
    text(d, G_LSTICK, "WASD", 15, G_LINE)
    x, y = G_RSTICK
    d.rounded_rectangle((x - 10, y - 15, x + 10, y + 15), 10, outline=G_LINE, width=2)
    d.line((x, y - 15, x, y - 4), fill=G_LINE, width=2)
    x0, y0, x1, y1 = G_TOUCHPAD
    text(d, ((x0 + x1) / 2, (y0 + y1) / 2), "TAB", 28, G_LINE)
    return im


def write(im, folder, name, preview_size):
    rgba = np.asarray(im, np.uint8)
    out = os.path.join(PROFILE, "sotc_textures", folder)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name + ".bin"), "wb") as f:
        f.write(encode(rgba))
    preview = Image.new("RGBA", im.size, (40, 90, 40, 255))
    preview.alpha_composite(im)
    preview.resize(preview_size, Image.LANCZOS).save(os.path.join(HERE, f"icons_{name}_{folder}.png"))


write(xbox(), "xbox", HASH, (768, 384))
write(keyboard(), "keyboard", HASH, (768, 384))
write(guide_xbox(), "xbox", GUIDE, (1920, 1080))
write(guide_keyboard(), "keyboard", GUIDE, (1920, 1080))
print("written to", PROFILE)
