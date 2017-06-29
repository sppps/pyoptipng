#!/usr/bin/env python
from pyoptipng import compress_png

with open('test.png', 'rb') as png:
    with open('out.png', 'wb+') as out:
        out.write(compress_png(png.read()))
