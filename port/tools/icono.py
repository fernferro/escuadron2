#!/usr/bin/env python3
"""Genera port/launcher/icono.ico e icono.png: estelas de hiperespacio y una nave propia (sin textos ni marcas)."""
import math, random, sys, os
from PIL import Image, ImageDraw, ImageFont, ImageFilter
sys.path.insert(0, os.path.dirname(__file__))
from nave import *
RAIZ = os.path.join(os.path.dirname(__file__), '..', '..')
S = 1024

def fondo(S, estelas=260):
    random.seed(7)
    bg = Image.new('RGBA', (S, S), (6, 6, 10, 255)); d = ImageDraw.Draw(bg)
    cx = cy = S / 2; k = S / 1024
    for i in range(estelas):
        a = random.uniform(0, 2 * math.pi); r0 = random.uniform(90, 520) * k; l = random.uniform(30, 150) * (r0 / (300 * k)) * k
        d.line((cx + math.cos(a) * r0, cy + math.sin(a) * r0, cx + math.cos(a) * (r0 + l), cy + math.sin(a) * (r0 + l)),
               fill=(235, 240, 255, int(min(255, 90 + r0 / k / 3))), width=max(1, int(r0 / 90)))
    glow = Image.new('RGBA', (S, S), (0, 0, 0, 0)); ImageDraw.Draw(glow).ellipse((cx - 260 * k, cy - 260 * k, cx + 260 * k, cy + 260 * k), fill=(70, 90, 140, 90))
    bg = Image.alpha_composite(bg, glow.filter(ImageFilter.GaussianBlur(120 * k)))
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    m = Image.new('L', (S, S), 0); ImageDraw.Draw(m).rounded_rectangle((S * .024, S * .024, S * .976, S * .976), radius=S * .2, fill=255)
    out.paste(bg, (0, 0), m)
    return out

def nave(img, cx, cy, r, rojo=(200, 26, 32, 255), blanco=(238, 238, 232, 255)):
    d = ImageDraw.Draw(img)
    T = lambda p: [(cx + x * r, cy + y * r) for x, y in p]
    for x, y in MOTORES:
        gl = Image.new('RGBA', img.size, (0, 0, 0, 0))
        ImageDraw.Draw(gl).ellipse((cx + x * r - r * .16, cy + y * r - r * .1, cx + x * r + r * .16, cy + y * r + r * .3), fill=(255, 60, 40, 200))
        img.alpha_composite(gl.filter(ImageFilter.GaussianBlur(r * .06)))
    for p in (ALA_D, CANARD_D):
        d.polygon(T(p), fill=blanco); d.polygon(T(espejo(p)), fill=blanco)
    d.polygon(T(ALETA_D), fill=rojo); d.polygon(T(espejo(ALETA_D)), fill=rojo)
    d.polygon(T(CUERPO), fill=blanco)
    d.polygon(T(CABINA), fill=(30, 40, 60, 255))
    d.line(T([(0, -0.15), (0, 0.7)]), fill=rojo, width=max(1, int(r * .05)))

def palabra(im, f, t, ytop, hgt, S):
    bb = f.getbbox(t); w, h = bb[2] - bb[0], bb[3] - bb[1]
    capa = Image.new('L', (w + 20, h + 20), 0); ImageDraw.Draw(capa).text((10 - bb[0], 10 - bb[1]), t, font=f, fill=255)
    capa = capa.resize((int(S * 0.80), hgt))
    x = (S - capa.width) // 2
    im.paste(Image.new('RGBA', capa.size, (0, 0, 0, 200)), (x + 6, ytop + 8), capa.filter(ImageFilter.GaussianBlur(6)))
    im.paste(Image.new('RGBA', capa.size, (242, 242, 238, 255)), (x, ytop), capa)

grande = fondo(S)
f = ImageFont.truetype(os.path.join(RAIZ, 'port/launcher/fuentes/Poppins-Bold.ttf'), 230)
nave(grande, S / 2, 470, 330)
chico = fondo(256, 120)
nave(chico, 128, 122, 100)
tam = [256, 128, 64, 48]
imgs = {s: grande.resize((s, s), Image.LANCZOS) for s in tam}
for s in (32, 24, 16): imgs[s] = chico.resize((s, s), Image.LANCZOS)
imgs[256].save(os.path.join(RAIZ, 'port/launcher/icono.ico'), sizes=[(s, s) for s in (256, 128, 64, 48, 32, 24, 16)],
               append_images=[imgs[s] for s in (128, 64, 48, 32, 24, 16)])
grande.resize((512, 512), Image.LANCZOS).save(os.path.join(RAIZ, 'port/launcher/icono.png'))
if len(sys.argv) > 1:
    grande.save(sys.argv[1] + '_1024.png'); chico.save(sys.argv[1] + '_chico.png')
