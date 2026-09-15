#!/usr/bin/env python3
"""Builds the MAME artwork for CZ-101 Studio.

Input : Resources/artwork-src/cz101-panel.svg   (1600 x 500 user units)
Output: Resources/artwork/cz101/cz101-panel.png  (rendered by Chrome at 2x)
        Resources/artwork/cz101/default.lay      (clickable buttons/keys, LEDs, LCD)

The SVG is only the picture.  Everything that must react (buttons, keys, LEDs,
the HD44780 LCD) is placed by default.lay on top of it, using the coordinates
below, which mirror the SVG.  If a control moves in the SVG, update it here.

Input tags/masks and LED names come from MAME's src/mame/casio/cz101.cpp and
src/mame/layout/cz101.lay.

After running this, press "Reiniciar" in the app to reload the artwork.
"""

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SVG = ROOT / "Resources" / "artwork-src" / "cz101-panel.svg"
OUT = ROOT / "Resources" / "artwork" / "cz101"
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
WIDTH, HEIGHT, SCALE = 1600, 500, 2

# ---------------------------------------------------------------------------
# Panel buttons: (label, x, y, w, h, inputtag, inputmask, led, led_x, led_y)
# led_x/led_y = LED centre; None places it just above the button.
# ---------------------------------------------------------------------------
BUTTONS = []


def button(label, x, y, w, h, tag, mask, led=None, led_x=None, led_y=None):
    BUTTONS.append((label, x, y, w, h, tag, mask, led, led_x, led_y))


# EFFECT
button("BEND RANGE", 175, 100, 36, 24, "kc9", 0x08)
button("VIBRATO ON/OFF", 225, 100, 36, 24, "kc9", 0x04, "led_14")
button("PORTAMENTO TIME", 175, 163, 36, 24, "kc9", 0x02)
button("PORTAMENTO ON/OFF", 225, 163, 36, 24, "kc9", 0x01, "led_15")

# PROGRAMMER - memory (labels are above these buttons, LEDs go above the labels)
button("PRESET", 315, 98, 32, 20, "kc9", 0x10, "led_11", led_y=84)
button("INTERNAL", 357, 98, 32, 20, "kc9", 0x20, "led_10", led_y=84)
button("CARTRIDGE", 399, 98, 32, 20, "kc9", 0x40, "led_9", led_y=84)
button("COMPARE/RECALL", 470, 98, 30, 20, "kc9", 0x80, "led_8", led_y=91)

# PROGRAMMER - tones 1..8 (LED names follow MAME's matrix wiring)
TONE_LEDS = ["led_tone2.0", "led_tone1.0", "led_tone0.0", "led_tone0.2",
             "led_tone2.1", "led_tone1.1", "led_tone0.1", "led_tone1.2"]
for i in range(8):
    button(f"TONE {i + 1}", 305 + 31 * i, 150, 27, 22, "kc11", 1 << i, TONE_LEDS[i])
button("SELECT", 563, 150, 32, 22, "kc10", 0x40, "led_7", led_y=180)

# PROGRAMMER - bottom row
button("SOLO", 315, 210, 32, 22, "kc10", 0x01, "led_13")
button("TONE MIX", 357, 210, 34, 22, "kc10", 0x02, "led_12")
button("KEY TRANSPOSE", 401, 210, 34, 22, "kc10", 0x04)
button("WRITE", 485, 210, 32, 22, "kc10", 0x08)
button("MIDI", 537, 210, 32, 22, "kc10", 0x10)

# DATA ENTRY
button("VALUE DOWN/SAVE", 675, 210, 28, 22, "kc12", 0x01)
button("VALUE UP/LOAD", 711, 210, 28, 22, "kc12", 0x02)
button("CURSOR LEFT", 770, 210, 28, 22, "kc12", 0x04)
button("CURSOR RIGHT", 806, 210, 28, 22, "kc12", 0x08)

# ENV STEP / ENV POINT / INITIALIZE
button("ENV STEP DOWN", 902, 92, 27, 20, "kc12", 0x10)
button("ENV STEP UP", 935, 92, 27, 20, "kc12", 0x20)
button("ENV POINT SUSTAIN", 902, 148, 27, 20, "kc12", 0x40)
button("ENV POINT END", 935, 148, 27, 20, "kc12", 0x80)
button("INITIALIZE", 916, 212, 32, 22, "kc14", 0x01)

# PARAMETER - line 1 (LEDs sit inside the blue DCO/DCW/DCA bars)
button("VIBRATO", 982, 112, 32, 22, "kc13", 0x02)
button("DCO1 WAVE FORM", 1035, 112, 34, 22, "kc13", 0x04, "led_env2.3", led_y=102.5)
button("DCO1 ENV", 1080, 112, 34, 22, "kc13", 0x08, "led_env1.3", led_y=102.5)
button("DCW1 KEY FOLLOW", 1140, 112, 34, 22, "kc13", 0x10, "led_env0.3", led_y=102.5)
button("DCW1 ENV", 1185, 112, 34, 22, "kc13", 0x20, "led_env2.2", led_y=102.5)
button("DCA1 KEY FOLLOW", 1245, 112, 34, 22, "kc13", 0x40, "led_env1.2", led_y=102.5)
button("DCA1 ENV", 1290, 112, 34, 22, "kc13", 0x80, "led_env0.2", led_y=102.5)
button("DETUNE", 1355, 112, 30, 22, "kc15", 0x01)

# PARAMETER - line 2
button("OCTAVE", 982, 176, 32, 22, "kc14", 0x02)
button("DCO2 WAVE FORM", 1035, 176, 34, 22, "kc14", 0x04, "led_env2.1", led_y=166.5)
button("DCO2 ENV", 1080, 176, 34, 22, "kc14", 0x08, "led_env1.1", led_y=166.5)
button("DCW2 KEY FOLLOW", 1140, 176, 34, 22, "kc14", 0x10, "led_env0.1", led_y=166.5)
button("DCW2 ENV", 1185, 176, 34, 22, "kc14", 0x20, "led_env2.0", led_y=166.5)
button("DCA2 KEY FOLLOW", 1245, 176, 34, 22, "kc14", 0x40, "led_env1.0", led_y=166.5)
button("DCA2 ENV", 1290, 176, 34, 22, "kc14", 0x80, "led_env0.0", led_y=166.5)

# LINE SELECT / MODULATION
button("LINE SELECT", 1425, 112, 34, 22, "kc15", 0x02)
button("RING", 1411, 188, 30, 22, "kc15", 0x04, "led_1")
button("NOISE", 1449, 188, 30, 22, "kc15", 0x08, "led_2")

# MASTER TUNE
button("MASTER TUNE DOWN", 1375, 36, 22, 15, "kc15", 0x10)
button("MASTER TUNE UP", 1408, 36, 22, 15, "kc15", 0x20)

# LEDs that are not tied to a single button: line select 1 / 2 / 1+2' / 1+1'
EXTRA_LEDS = [("led_6", 1415, 103), ("led_5", 1429, 103), ("led_4", 1447, 103), ("led_3", 1467, 103)]
GREEN_LEDS = {"led_7"}

# ---------------------------------------------------------------------------
# Keyboard: 49 keys C2..C6, same geometry as the SVG (group translate 20,305)
# ---------------------------------------------------------------------------
KEY_X, KEY_Y = 20, 309
WHITE_STEP, WHITE_W, WHITE_H = 53.8, 52.5, 184
BLACK_W, BLACK_H = 28, 116
BLACK_X = [36, 94, 199, 254, 310, 412, 470, 575, 630, 686,
           788, 846, 951, 1006, 1062, 1164, 1222, 1327, 1382, 1438]

# MAME key matrix: per octave C D E F on kcN, G A B on kcN+1 (white);
# C# D# on kcN, F# G# A# on kcN+1 (black).
WHITE_TAGS, BLACK_TAGS = [], []
for octave in range(4):
    a, b = f"kc{octave * 2}", f"kc{octave * 2 + 1}"
    WHITE_TAGS += [(a, 0x01), (a, 0x04), (a, 0x10), (a, 0x20), (b, 0x02), (b, 0x08), (b, 0x20)]
    BLACK_TAGS += [(a, 0x02), (a, 0x08), (b, 0x01), (b, 0x04), (b, 0x10)]
WHITE_TAGS.append(("kc8", 0x01))  # C6

# LCD: MAME screen is 97 x 19 px; centred in the green window (658,84 194x52)
LCD = (663, 92, 184, 36)


def fmt(value):
    return f"{value:g}"


def bounds(x, y, w, h):
    return f'<bounds x="{fmt(x)}" y="{fmt(y)}" width="{fmt(w)}" height="{fmt(h)}" />'


def render_png():
    svg = SVG.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory() as tmp:
        html = pathlib.Path(tmp) / "panel.html"
        html.write_text(
            "<!doctype html><html><head><meta charset='utf-8'>"
            "<style>html,body{margin:0;padding:0;background:#18191b;overflow:hidden}"
            "svg{display:block}</style></head><body>" + svg + "</body></html>",
            encoding="utf-8")
        png = OUT / "cz101-panel.png"
        subprocess.run([
            CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
            f"--force-device-scale-factor={SCALE}", f"--window-size={WIDTH},{HEIGHT}",
            f"--screenshot={png}", html.as_uri()],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return png


def build_layout():
    out = ['<?xml version="1.0"?>',
           "<!-- Generated by tools/make_artwork.py from Resources/artwork-src/cz101-panel.svg. Do not edit by hand. -->",
           '<mamelayout version="2">',
           '\t<element name="panel"><image file="cz101-panel.png" /></element>',
           "",
           "\t<!-- pressed feedback: transparent until the input is active -->",
           '\t<element name="press_button" defstate="0">',
           '\t\t<rect state="1"><color red="0" green="0" blue="0" alpha="0.40" /></rect>',
           "\t</element>",
           '\t<element name="press_white" defstate="0">',
           '\t\t<rect state="1"><color red="0.25" green="0.22" blue="0.15" alpha="0.28" /></rect>',
           "\t</element>",
           '\t<element name="press_black" defstate="0">',
           '\t\t<rect state="1"><color red="1" green="1" blue="1" alpha="0.16" /></rect>',
           "\t</element>",
           "",
           "\t<!-- LEDs: 10x10 box, 5 unit lens, glow when lit -->",
           '\t<element name="led_red" defstate="0">',
           '\t\t<disk state="1"><bounds x="0" y="0" width="10" height="10" /><color red="1" green="0.2" blue="0.1" alpha="0.30" /></disk>',
           '\t\t<disk state="0"><bounds x="2.5" y="2.5" width="5" height="5" /><color red="0.22" green="0.05" blue="0.04" /></disk>',
           '\t\t<disk state="1"><bounds x="2.5" y="2.5" width="5" height="5" /><color red="1" green="0.25" blue="0.12" /></disk>',
           "\t</element>",
           '\t<element name="led_green" defstate="0">',
           '\t\t<disk state="1"><bounds x="0" y="0" width="10" height="10" /><color red="0.2" green="1" blue="0.2" alpha="0.30" /></disk>',
           '\t\t<disk state="0"><bounds x="2.5" y="2.5" width="5" height="5" /><color red="0.04" green="0.18" blue="0.05" /></disk>',
           '\t\t<disk state="1"><bounds x="2.5" y="2.5" width="5" height="5" /><color red="0.3" green="1" blue="0.3" /></disk>',
           "\t</element>",
           "",
           '\t<view name="CZ-101 Studio">',
           f'\t\t<element ref="panel">{bounds(0, 0, WIDTH, HEIGHT)}</element>',
           f'\t\t<screen index="0" blend="alpha">{bounds(*LCD)}</screen>',
           ""]

    for label, x, y, w, h, tag, mask, _led, _lx, _ly in BUTTONS:
        out.append(f'\t\t<!-- {label} -->')
        out.append(f'\t\t<element ref="press_button" inputtag="{tag}" inputmask="0x{mask:02x}">{bounds(x, y, w, h)}</element>')

    out.append("")
    out.append("\t\t<!-- keyboard: white keys first so black keys win the overlap -->")
    for i, (tag, mask) in enumerate(WHITE_TAGS):
        out.append(f'\t\t<element ref="press_white" inputtag="{tag}" inputmask="0x{mask:02x}">'
                   f'{bounds(KEY_X + WHITE_STEP * i, KEY_Y, WHITE_W, WHITE_H)}</element>')
    for x, (tag, mask) in zip(BLACK_X, BLACK_TAGS):
        out.append(f'\t\t<element ref="press_black" inputtag="{tag}" inputmask="0x{mask:02x}">'
                   f'{bounds(KEY_X + x, KEY_Y, BLACK_W, BLACK_H)}</element>')

    out.append("")
    leds = [(led, x + w / 2 if lx is None else lx, y - 6 if ly is None else ly)
            for _label, x, y, w, h, _tag, _mask, led, lx, ly in BUTTONS if led]
    leds += EXTRA_LEDS
    for name, cx, cy in leds:
        ref = "led_green" if name in GREEN_LEDS else "led_red"
        out.append(f'\t\t<element ref="{ref}" name="{name}">{bounds(cx - 5, cy - 5, 10, 10)}</element>')

    out += ["\t</view>", "</mamelayout>", ""]
    return "\n".join(out), len(leds)


def main():
    if not SVG.exists():
        sys.exit(f"SVG not found: {SVG}")
    if not pathlib.Path(CHROME).exists():
        sys.exit(f"Google Chrome not found: {CHROME}")

    OUT.mkdir(parents=True, exist_ok=True)
    png = render_png()
    layout, led_count = build_layout()
    (OUT / "default.lay").write_text(layout, encoding="utf-8")

    keys = len(WHITE_TAGS) + len(BLACK_TAGS)
    print(f"{png} rendered")
    print(f"{OUT / 'default.lay'}: {len(BUTTONS)} buttons, {keys} keys, {led_count} LEDs")


if __name__ == "__main__":
    main()
