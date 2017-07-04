#!/usr/bin/env python
from pyoptipng import compress_png, advpng

with open('test.png', 'rb') as png:
    with open('out.png', 'wb+') as out:
        out.write(compress_png(png.read(), 7))

with open('out.png', 'rb') as png:
    with open('out2.png', 'wb+') as out:
        out.write(advpng(png.read()))
