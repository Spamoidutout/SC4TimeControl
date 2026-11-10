#!/usr/bin/env python3
"""build_dat.py - builds SC4TimeOfDayMenu.dat (requires Pillow).

  python3 build_dat.py ../assets/maxis_dialog_source.png ../dist/SC4TimeOfDayMenu.dat

The .dat holds the PNG pieces of the frame and panel, cut from a real game
dialog (magenta = transparent, the source alpha is kept: the frame's rim is
semi-transparent), and one UI script (LEGACY text format) that places them
with a title and two columns of 12 hours. The DLL fills the texts and reads
the clicks against the transparent hit boxes.

The ids must match src/TimeOfDayMenu.cpp.
"""
import io
import struct
import sys

from PIL import Image

# ---- ids ---------------------------------------------------------------------
PNG_GROUP = 0x66912E09
UI_GROUP = 0xB99FCED4
MENU = 0x1
MENU_ROOT = 0x6D366D2D
TITLE = 0x10
HOUR_TEXT_FIRST = 0x100          # + hour
HOUR_HIT_FIRST = 0x200           # + hour
BG_OUTER, BG_PANEL = 0x1000, 0x1010   # + 1..9
PNG_OUTER, PNG_PANEL = 0x10, 0x20     # + 0..8

T_PNG, T_UI = 0x856DDBAC, 0x00000000

# ---- source image (maxis_dialog_source.png, 434x381) ---------------------------
OUTER_CORNER = 12
PANEL_RECT, PANEL_CORNER = (56, 32, 362, 199), 10
TILE = 16

# ---- layout (pixels) -----------------------------------------------------------
TITLE_W = 69                     # estimated width of "Time of Day" in GenButton
LABEL_W = 34                     # estimated width of "00:00"
COL_W, COL_GAP = 72, 12
PAD_X, PAD_TOP, PAD_BOTTOM = 10, 8, 6
ROW_PITCH, ROW_H, ROWS = 20, 18, 12
PANEL_X, PANEL_Y = 12, 32
PANEL_W = PAD_X + COL_W + COL_GAP + COL_W + PAD_X
PANEL_H = PAD_TOP + ROWS * ROW_PITCH + PAD_BOTTOM
MENU_W = PANEL_X + PANEL_W + 12
MENU_H = PANEL_Y + PANEL_H + 12

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
    title_l = (MENU_W - TITLE_W) // 2
    kids += text(TITLE, title_l, 9, MENU_W - title_l - 12, 18, TEXT_FILL_OUTER)
    kids += nine(BG_PANEL, PNG_PANEL, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL_CORNER)
    off = (COL_W - LABEL_W) // 2
    for h in range(24):
        l, t = cell(h)
        kids += text(HOUR_TEXT_FIRST + h, l + off, t, COL_W - off, ROW_H, TEXT_FILL_PANEL)
    for h in range(24):
        l, t = cell(h)
        kids += box(HOUR_HIT_FIRST + h, l, t - 1, COL_W, ROW_PITCH)
    # ignoremouse=no on the root: the menu takes the clicks (not the city below)
    return ('<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x%08x %s fillcolor=(0,0,0) %s '
            'blttype=tiled %s fill=no outline=no paint=yes sidebar=no gutters=(0,0) >\r\n'
            '<CHILDREN>\r\n%s</CHILDREN>\r\n'
            % (MENU_ROOT, area(0, 0, MENU_W, MENU_H), flags(ignoremouse='no'), GEN_TAIL, kids))


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
    open(sys.argv[2], 'wb').write(dbpf(records))
    print('%s: %d records, menu %dx%d' % (sys.argv[2], len(records), MENU_W, MENU_H))


if __name__ == '__main__':
    main()
