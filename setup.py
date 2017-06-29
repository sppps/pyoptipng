#!/usr/bin/env python
import os
from setuptools import setup, Extension, Command
from distutils.command import build_py, build_ext, clean
from distutils.util import get_platform


all_sources = ['src/main.c',
               # 'optipng/src/optipng/optim.c',
               'optipng/src/optipng/bitset.c',
               'optipng/src/opngreduc/opngreduc.c',
               'optipng/src/optipng/ratio.c',
               'optipng/src/optipng/osys.c',
               'optipng/src/libpng/png.c',
               'optipng/src/libpng/pngread.c',
               'optipng/src/libpng/pngwrite.c',
               'optipng/src/libpng/pngerror.c',
               'optipng/src/libpng/pngrutil.c',
               'optipng/src/libpng/pngmem.c',
               'optipng/src/libpng/pngwutil.c',
               'optipng/src/libpng/pngtrans.c',
               'optipng/src/libpng/pngrtran.c',
               'optipng/src/libpng/pngwio.c',
               'optipng/src/libpng/pngget.c',
               'optipng/src/libpng/pngrio.c',
               'optipng/src/libpng/pngset.c',
               'optipng/src/pngxtern/pngxmem.c',
               'optipng/src/pngxtern/pngxread.c',
               'optipng/src/pngxtern/pngxrbmp.c',
               'optipng/src/pngxtern/pngxrgif.c',
               'optipng/src/gifread/gifread.c',
               'optipng/src/pngxtern/pngxrjpg.c',
               'optipng/src/pngxtern/pngxrpnm.c',
               'optipng/src/pngxtern/pngxrtif.c',
               'optipng/src/minitiff/tiffbase.c',
               'optipng/src/minitiff/tiffread.c',
               'optipng/src/pngxtern/pngxset.c',
               'optipng/src/pnmio/pnmin.c',
               'optipng/src/pnmio/pnmutil.c',
               ]

pyoptipng_module = Extension('pyoptipng/_pyoptipng',
                                sources=all_sources,
                                include_dirs=[
                                   os.path.join(os.path.dirname(os.path.abspath(__file__)), 'optipng', 'src', 'optipng'),
                                   os.path.join(os.path.dirname(os.path.abspath(__file__)), 'optipng', 'src', 'opngreduc'),
                                   os.path.join(os.path.dirname(os.path.abspath(__file__)), 'optipng', 'src', 'pngxtern'),
                                   os.path.join(os.path.dirname(os.path.abspath(__file__)), 'optipng', 'src', 'cexcept'),
                                   os.path.join(os.path.dirname(os.path.abspath(__file__)), 'optipng', 'src', 'zlib'),
                                   os.path.join(os.path.dirname(os.path.abspath(__file__)), 'optipng', 'src', 'gifread'),
                                   os.path.join(os.path.dirname(os.path.abspath(__file__)), 'optipng', 'src', 'pnmio'),
                                   os.path.join(os.path.dirname(os.path.abspath(__file__)), 'optipng', 'src', 'minitiff'),
                                ])

if __name__ == '__main__':
    setup(name='pyoptipng',
          version='0.1.0',
          description='Bindings for PNG optimization utilities',
          classifiers=[
            'Development Status :: 4 - Beta',
            'License :: OSI Approved :: MIT License',
            'Programming Language :: Python :: 2.7',
            'Topic :: System :: Archiving :: Compression',
            'Topic :: Software Development :: Libraries :: Python Modules',
          ],
          keywords='png optipng advancecomp image compression binding',
          url='https://github.com/sppps/pyoptipng',
          author='Sergey S. Gogin',
          author_email='sppps@sppps.ru',
          license='MIT',
          packages=['pyoptipng'],
          ext_modules=[pyoptipng_module],
          include_package_data=True,
          zip_safe=False)
