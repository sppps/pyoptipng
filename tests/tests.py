#!/usr/bin/env python
import pyoptipng
import os

# for dirname, dirnames, files in os.walk('./PngSuite'):
#     for f in files:
#         filepath = os.path.join(dirname, f)
#         print filepath
#         try:
#             with open(filepath, 'rb') as png:
#                 pyoptipng.mc_compress_png(png.read(), 3)
#         except Exception as e:
#             print e

FILE = 'test'

with open(FILE+'.png', 'rb') as png:
    with open(FILE+'_out.png', 'wb+') as out:
        out.write(pyoptipng.mc_compress_png(png.read(), 4))

# with open('bananapixifactory.png', 'rb') as png:
#     with open('bananapixifactory_out.png', 'wb+') as out:
#         out.write(compress_png(png.read(), 3))

# with open('bananapixifactory_out.png', 'rb') as png:
#     with open('bananapixifactory_out_2.png', 'wb+') as out:
#         out.write(advpng(png.read()))
