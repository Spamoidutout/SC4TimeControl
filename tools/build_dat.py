#!/usr/bin/env python3
"""build_dat.py - builds SC4TimeControl.dat (requires Pillow).

  python3 build_dat.py ../assets/maxis_dialog_source.png ../assets/clock_field.png ../dist/SC4TimeControl.dat

The .dat holds the PNG pieces of the frame and panel, cut from a real game
dialog (magenta = transparent, the source alpha is kept: the frame's rim is
semi-transparent), and one UI script (LEGACY text format) that places them
with a title, two columns of 12 hours and a row of cycle speeds. The DLL fills
the texts and reads the clicks against the transparent hit boxes.

A second UI script is the clock shown next to the date: clock_field.png (the
right end of the date field, widened) and a text window for the time. The DLL
puts it in the game's bottom-left panel (kClockX, kClockY in
src/TimeControl.cpp).

The ids must match src/TimeControl.cpp.
"""
import io
import struct
import sys

from PIL import Image

# ---- ids ---------------------------------------------------------------------
PNG_GROUP = 0x64DD2E60
UI_GROUP = 0x892F59E5
MENU = 0x1
MENU_ROOT = 0x857A0EB3
CLOCK = 0x2
CLOCK_ROOT = 0x3D6A3173
CLOCK_FIELD, CLOCK_TEXT = 0x20, 0x21
PNG_CLOCK = 0x40
TITLE = 0x10
SPEED_TITLE = 0x11
HOUR_TEXT_FIRST = 0x100          # + hour
HOUR_HIT_FIRST = 0x200           # + hour
SPEED_TEXT_FIRST = 0x300         # + speed index
SPEED_HIT_FIRST = 0x310          # + speed index
BG_OUTER, BG_PANEL, BG_SPEEDS = 0x1000, 0x1010, 0x1020   # + 1..9
PNG_OUTER, PNG_PANEL = 0x10, 0x20     # + 0..8

T_PNG, T_UI = 0x856DDBAC, 0x00000000

# ---- source image (maxis_dialog_source.png, 434x381) ---------------------------
OUTER_CORNER = 12
PANEL_RECT, PANEL_CORNER = (56, 32, 362, 199), 10
TILE = 16

# ---- layout (pixels) -----------------------------------------------------------
LABEL_W = 34                     # estimated width of "00:00"
COL_W, COL_GAP = 72, 12
PAD_X, PAD_TOP, PAD_BOTTOM = 10, 8, 6
ROW_PITCH, ROW_H, ROWS = 20, 18, 12
PANEL_X, PANEL_Y = 12, 32
PANEL_W = PAD_X + COL_W + COL_GAP + COL_W + PAD_X
PANEL_H = PAD_TOP + ROWS * ROW_PITCH + PAD_BOTTOM
MENU_W = PANEL_X + PANEL_W + 12

# speed row: a title on the blue, then a one-row panel
SPEED_LABELS = ['Pause', '1/2', 'x1', 'x2', 'x4', 'x8']   # same order as the DLL
SPEED_TITLE_Y = PANEL_Y + PANEL_H + 6
SPEEDS_Y = SPEED_TITLE_Y + 20
SPEEDS_H = 4 + ROW_H + 4
MENU_H = SPEEDS_Y + SPEEDS_H + 12

# Measured in game on screenshots (text windows only):
TITLE_DX = -4                    # "Time of Day", "Cycle Speed"
HOUR_COL_DX = (-4, -2)           # left / right column of hours
SPEED_INK_W = [35, 20, 13, 16, 16, 14]   # ink width of each speed label (GenButton)
SPEED_INK_OFF = [2, 3, 1, 1, 1, 1]       # text window's left edge -> first ink pixel

TEXT_FILL_PANEL = (189, 207, 211)
TEXT_FILL_OUTER = (156, 178, 194)


# ---- PNG pieces ----------------------------------------------------------------
def keyed(img):
    """Magenta -> transparent; every other pixel keeps its alpha."""
    px = img.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            r, g, b, a = px[x, y]
            if (r, g, b) == (255, 0, 255) or a == 0:
                px[x, y] = (0, 0, 0, 0)
    return img


def cut_nine(src, x, y, w, h, c):
    """Corners c*c; edges and centre TILE long, cut from a straight part."""
    mx, my = x + c + 4, y + c + 4
    boxes = [(x, y, c, c), (mx, y, TILE, c), (x + w - c, y, c, c),
             (x, my, c, TILE), (mx, my, TILE, TILE), (x + w - c, my, c, TILE),
             (x, y + h - c, c, c), (mx, y + h - c, TILE, c), (x + w - c, y + h - c, c, c)]
    return [keyed(src.crop((bx, by, bx + bw, by + bh))) for bx, by, bw, bh in boxes]


def png(img):
    b = io.BytesIO()
    img.save(b, 'PNG', optimize=True)
    return b.getvalue()


# ---- UI script -----------------------------------------------------------------
FLAGS = dict(visible='yes', enabled='yes', moveable='no', sizeable='no', sortable='no', pbuff='no',
             pbufftrans='yes', pbufferase='yes', pbuffvid='no', alphablend='yes', acceptfocus='no',
             mousetrans='no', ignoremouse='yes')
GEN_TAIL = ('userdata=0 moveable=no sizeable=no defaultkeys=no closevisible=no gobackvisible=no '
            'minmaxvisible=no closedisabled=no gobackdisabled=no minmaxdisabled=no titlebar=no')


def flags(**kw):
    f = dict(FLAGS, **kw)
    return ' '.join('winflag_%s=%s' % kv for kv in f.items())


def area(l, t, w, h):
    return 'area=(%d,%d,%d,%d)' % (l, t, l + w, t + h)   # left, top, right, bottom


def bmp(wid, l, t, w, h, inst):
    return ('   <LEGACY clsid=GZWinBMP iid=IGZWinBMP id=0x%08x %s fillcolor=(0,0,0) %s '
            'image={%08x,%08x} imagerect=(0,0,%d,%d) notify=no transparentbkg=yes edgeimage=no >\r\n'
            % (wid, area(l, t, w, h), flags(), PNG_GROUP, inst, w, h))


def tiled(wid, l, t, w, h, inst):
    return ('   <LEGACY clsid=GZWinGen iid=IGZWinGen id=0x%08x %s fillcolor=(0,0,0) %s '
            'image={%08x,%08x} blttype=tiled %s fill=yes outline=no paint=yes sidebar=no gutters=(0,0) >\r\n'
            % (wid, area(l, t, w, h), flags(), PNG_GROUP, inst, GEN_TAIL))


def box(wid, l, t, w, h):
    return ('   <LEGACY clsid=GZWinGen iid=IGZWinGen id=0x%08x %s fillcolor=(0,0,0) %s '
            'blttype=tiled %s fill=no outline=no paint=no sidebar=no gutters=(0,0) >\r\n'
            % (wid, area(l, t, w, h), flags(), GEN_TAIL))


def text(wid, l, t, w, h, fill):
    return ('   <LEGACY clsid=GZWinText iid=IGZWinText id=0x%08x %s fillcolor=(%d,%d,%d) %s font=GenButton >\r\n'
            % (wid, area(l, t, w, h), fill[0], fill[1], fill[2], flags()))


def estimate_width(t):
    """Pixels of a GenButton text (same estimate as the kit)."""
    return sum(4 if ch in " iljtf:.'" else 10 if ch in "mwMW" else 8 if ch.isupper() else 7 for ch in t)


def centred_text(wid, l, t, w, label, fill):
    """Text centred on [l, l + w], with its estimated width."""
    off = max(2, (w - estimate_width(label)) // 2)
    return text(wid, l + off, t, w - off, ROW_H, fill)


def nine(first_id, png_first, x, y, w, h, c):
    rects = [(x, y, c, c), (x + c, y, w - 2 * c, c), (x + w - c, y, c, c),
             (x, y + c, c, h - 2 * c), (x + c, y + c, w - 2 * c, h - 2 * c), (x + w - c, y + c, c, h - 2 * c),
             (x, y + h - c, c, c), (x + c, y + h - c, w - 2 * c, c), (x + w - c, y + h - c, c, c)]
    out = ''
    for k, r in enumerate(rects):
        out += (bmp if k in (0, 2, 6, 8) else tiled)(first_id + 1 + k, *r, png_first + k)
    return out


def cell(h):
    col, row = divmod(h, ROWS)
    return PANEL_X + PAD_X + col * (COL_W + COL_GAP), PANEL_Y + PAD_TOP + row * ROW_PITCH


def menu_script():
    kids = nine(BG_OUTER, PNG_OUTER, 0, 0, MENU_W, MENU_H, OUTER_CORNER)
    kids += centred_text(TITLE, TITLE_DX, 9, MENU_W, 'Time of Day', TEXT_FILL_OUTER)
    kids += nine(BG_PANEL, PNG_PANEL, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL_CORNER)
    off = (COL_W - LABEL_W) // 2
    for h in range(24):
        l, t = cell(h)
        kids += text(HOUR_TEXT_FIRST + h, l + off + HOUR_COL_DX[h // ROWS], t, COL_W - off, ROW_H, TEXT_FILL_PANEL)
    for h in range(24):
        l, t = cell(h)
        kids += box(HOUR_HIT_FIRST + h, l, t - 1, COL_W, ROW_PITCH)
    kids += centred_text(SPEED_TITLE, TITLE_DX, SPEED_TITLE_Y, MENU_W, 'Cycle Speed', TEXT_FILL_OUTER)
    kids += nine(BG_SPEEDS, PNG_PANEL, PANEL_X, SPEEDS_Y, PANEL_W, SPEEDS_H, PANEL_CORNER)
    # speeds: equal gaps between the labels and at both ends, inside the panel's
    # border; text windows run 12 px past the ink (they are transparent)
    inner_l, inner_r = PANEL_X + 1, PANEL_X + PANEL_W - 1
    gap = (inner_r - inner_l - sum(SPEED_INK_W)) / (len(SPEED_LABELS) + 1)
    x = inner_l + gap
    for i, (w, off) in enumerate(zip(SPEED_INK_W, SPEED_INK_OFF)):
        ink = round(x)
        kids += text(SPEED_TEXT_FIRST + i, ink - off, SPEEDS_Y + 4, w + off + 12, ROW_H, TEXT_FILL_PANEL)
        hit_l = round(x - gap / 2)
        hit_r = round(x + w + gap / 2)
        kids += box(SPEED_HIT_FIRST + i, hit_l, SPEEDS_Y + 3, hit_r - hit_l, ROW_PITCH)
        x += w + gap
    # ignoremouse=no on the root: the menu takes the clicks (not the city below)
    return ('<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x%08x %s fillcolor=(0,0,0) %s '
            'blttype=tiled %s fill=no outline=no paint=yes sidebar=no gutters=(0,0) >\r\n'
            '<CHILDREN>\r\n%s</CHILDREN>\r\n'
            % (MENU_ROOT, area(0, 0, MENU_W, MENU_H), flags(ignoremouse='no'), GEN_TAIL, kids))


# ---- the clock next to the date --------------------------------------------------
CLOCK_W, CLOCK_H = 49, 37        # field piece (48x37) at x=1, text at x=0
CLOCK_TEXT_RECT = (0, 12, 38, 15)  # same place and height as the game's date text: same glyph rows


def clock_script():
    kids = bmp(CLOCK_FIELD, 1, 0, 48, 37, PNG_CLOCK)
    # the DLL gives it the font style of the game's date text
    kids += ('   <LEGACY clsid=GZWinText iid=IGZWinText id=0x%08x %s fillcolor=(197,212,209) %s >\r\n'
             % (CLOCK_TEXT, area(*CLOCK_TEXT_RECT), flags()))
    # ignoremouse=yes on the root too: clicks and the date tooltip go to the game
    return ('<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x%08x %s fillcolor=(0,0,0) %s '
            'blttype=tiled %s fill=no outline=no paint=yes sidebar=no gutters=(0,0) >\r\n'
            '<CHILDREN>\r\n%s</CHILDREN>\r\n'
            % (CLOCK_ROOT, area(0, 0, CLOCK_W, CLOCK_H), flags(), GEN_TAIL, kids))


# ---- DBPF 1.0, index 7.0, uncompressed -------------------------------------------
def dbpf(records):
    body, entries, off = bytearray(), [], 96
    for t, g, i, data in records:
        entries.append((t, g, i, off, len(data)))
        body += data
        off += len(data)
    index = b''.join(struct.pack('<IIIII', *e) for e in entries)
    head = bytearray(96)
    head[0:4] = b'DBPF'
    struct.pack_into('<II', head, 4, 1, 0)
    struct.pack_into('<IIII', head, 32, 7, len(entries), off, len(index))
    return bytes(head) + bytes(body) + index


def main():
    src = Image.open(sys.argv[1]).convert('RGBA')
    outer = cut_nine(src, 0, 0, src.size[0], src.size[1], OUTER_CORNER)
    panel = cut_nine(src, *PANEL_RECT, PANEL_CORNER)
    records = [(T_PNG, PNG_GROUP, PNG_OUTER + k, png(img)) for k, img in enumerate(outer)]
    records += [(T_PNG, PNG_GROUP, PNG_PANEL + k, png(img)) for k, img in enumerate(panel)]
    records.append((T_UI, UI_GROUP, MENU, menu_script().encode('ascii')))
    field = Image.open(sys.argv[2]).convert('RGBA')   # used as is (re-encoded only to drop metadata)
    assert field.size == (48, 37)
    records.append((T_PNG, PNG_GROUP, PNG_CLOCK, png(field)))
    records.append((T_UI, UI_GROUP, CLOCK, clock_script().encode('ascii')))
    open(sys.argv[3], 'wb').write(dbpf(records))
    print('%s: %d records, menu %dx%d' % (sys.argv[3], len(records), MENU_W, MENU_H))


if __name__ == '__main__':
    main()
