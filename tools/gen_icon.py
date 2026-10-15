#!/usr/bin/env python3
"""Renderiza os vetores autorais do launcher em PNGs legados e uma prévia."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "android/app/src/main/res"
ANDROID = "{http://schemas.android.com/apk/res/android}"
SCALE = 8
SIZE = 108 * SCALE


def contours(data):
    tokens = re.findall(r"[MLCZ]|-?\d+(?:\.\d+)?", data)
    result, points = [], []
    cursor, command = 0, None
    while cursor < len(tokens):
        if tokens[cursor] in "MLCZ":
            command = tokens[cursor]
            cursor += 1
        if command == "Z":
            result.append(points)
            points = []
            command = None
            continue
        if command in ("M", "L"):
            point = tuple(float(v) for v in tokens[cursor:cursor + 2])
            cursor += 2
            points.append(point)
            command = "L"
        elif command == "C":
            p0 = points[-1]
            values = [float(v) for v in tokens[cursor:cursor + 6]]
            cursor += 6
            p1, p2, p3 = values[:2], values[2:4], values[4:]
            for i in range(1, 33):
                t, u = i / 32, 1 - i / 32
                points.append(tuple(u**3 * p0[k] + 3 * u*u*t * p1[k] +
                                    3 * u*t*t * p2[k] + t**3 * p3[k] for k in range(2)))
        else:
            raise ValueError(f"Comando de vetor não suportado: {command}")
    if points:
        result.append(points)
    return result


def rgb(value):
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def paths(node, sx=1, sy=1, tx=0, ty=0):
    if node.tag == "group":
        gx = float(node.get(ANDROID + "scaleX", "1"))
        gy = float(node.get(ANDROID + "scaleY", "1"))
        tx += sx * float(node.get(ANDROID + "pivotX", "0")) * (1 - gx)
        ty += sy * float(node.get(ANDROID + "pivotY", "0")) * (1 - gy)
        sx, sy = sx * gx, sy * gy
    if node.tag == "path":
        yield node, sx, sy, tx, ty
    else:
        for child in node:
            yield from paths(child, sx, sy, tx, ty)


def render(path):
    image = Image.new("RGBA", (SIZE, SIZE))
    for node, sx, sy, tx, ty in paths(ET.parse(path).getroot()):
        mask = Image.new("L", (SIZE, SIZE))
        draw = ImageDraw.Draw(mask)
        opacity = int(255 * float(node.get(ANDROID + "fillAlpha", "1")))
        for shape in contours(node.get(ANDROID + "pathData")):
            draw.polygon([((x * sx + tx) * SCALE, (y * sy + ty) * SCALE) for x, y in shape], fill=opacity)
        gradient = next((g for g in node.iter() if g.tag == "gradient"), None)
        if gradient is None:
            layer = Image.new("RGBA", (SIZE, SIZE), rgb(node.get(ANDROID + "fillColor")) + (255,))
        else:
            start = rgb(gradient.get(ANDROID + "startColor"))
            end = rgb(gradient.get(ANDROID + "endColor"))
            layer = Image.new("RGBA", (SIZE, SIZE))
            fill = ImageDraw.Draw(layer)
            for y in range(SIZE):
                t = y / (SIZE - 1)
                color = tuple(round(a * (1 - t) + b * t) for a, b in zip(start, end))
                fill.line([(0, y), (SIZE, y)], fill=color + (255,))
        layer.putalpha(mask)
        image = Image.alpha_composite(image, layer)
    return image


def main():
    image = Image.alpha_composite(render(RES / "drawable/ic_launcher_background.xml"),
                                  render(RES / "drawable/ic_launcher_foreground.xml"))
    # Adaptive foreground has 18dp of overscan; legacy icons use the visible 72dp tile.
    image = image.crop(tuple(v * SCALE for v in (18, 18, 90, 90)))
    mask = Image.new("L", image.size)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, image.width - 1, image.height - 1),
                                          radius=14 * SCALE, fill=255)
    image.putalpha(mask)
    for density, size in {"mdpi": 48, "hdpi": 72, "xhdpi": 96,
                          "xxhdpi": 144, "xxxhdpi": 192}.items():
        dest = RES / f"mipmap-{density}/ic_launcher.png"
        image.resize((size, size), Image.Resampling.LANCZOS).save(dest)
    preview = ROOT / "docs/img/icone.png"
    image.resize((512, 512), Image.Resampling.LANCZOS).save(preview)
    print(f"Ícones atualizados; prévia: {preview}")


if __name__ == "__main__":
    main()
