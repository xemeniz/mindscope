#!/usr/bin/env python3
"""Monta ui/index.html a partir de ui/parts/: la app + fuentes Nunito + skin MIND SERIES (v2).
skin.css es la parte común de la serie (igual en todos los plugins); skinvars.css lleva el color y el marco de MINDSCOPE."""
import pathlib
root = pathlib.Path(__file__).resolve().parent.parent / 'ui'
p = root / 'parts'
html = (p / 'app.html').read_text()
for tag, f in (('/*@FONTS@*/', 'fonts.css'), ('/*@SKINVARS@*/', 'skinvars.css'), ('/*@SKIN@*/', 'skin.css')):
    assert tag in html, tag
    html = html.replace(tag, (p / f).read_text().strip())
(root / 'index.html').write_text(html)
print('ui/index.html', len(html) // 1024, 'KB')
