#!/usr/bin/env python
import os
import subprocess
from setuptools import setup, Extension, Command
from distutils.command import build_py, build_ext, clean
from distutils.util import get_platform

WITH_OPTIPNG = False
WITH_ADVANCECOMP = True
WITH_MC_OPNG = True
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

libraries = []
all_sources = ['src/main.c']
defines = [
          ('PACKAGE', '"pyoptipng"'),
          ('VERSION', '"0.1.0"'),
          ('PACKAGE_URL', '"https://github.com/sppps/pyoptipng"'),
          ('HAVE_SYS_STAT_H', None),
          ('TIME_WITH_SYS_TIME', None),
          ('HAVE_SYS_TIME_H', None),
          ('HAVE_SYS_TYPES_H', None),
          ('HAVE_UTIME_H', None),
          ('HAVE_SNPRINTF', None),
          ('HAVE_VSNPRINTF', None),
          ('USE_ERROR_SILENT', None),
          ('HAVE_GETOPT', None),
          ]
include_dirs = []

if WITH_OPTIPNG:
    defines += [('PYOPTIPNG_WITH_OPTIPNG', None)]
    all_sources += ['src/optipng.c',
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
      'optipng/src/pnmio/pnmutil.c']
    include_dirs += [os.path.join(BASE_DIR, 'optipng', 'src', 'optipng'),
                     os.path.join(BASE_DIR, 'optipng', 'src', 'opngreduc'),
                     os.path.join(BASE_DIR, 'optipng', 'src', 'pngxtern'),
                     os.path.join(BASE_DIR, 'optipng', 'src', 'cexcept'),
                     os.path.join(BASE_DIR, 'optipng', 'src', 'zlib'),
                     os.path.join(BASE_DIR, 'optipng', 'src', 'gifread'),
                     os.path.join(BASE_DIR, 'optipng', 'src', 'pnmio'),
                     os.path.join(BASE_DIR, 'optipng', 'src', 'minitiff'),
                     os.path.join(BASE_DIR, 'optipng', 'src', 'libpng')
                     ]

if WITH_ADVANCECOMP:
    defines += [('PYOPTIPNG_WITH_ADVANCECOMP', None)]
    all_sources += ['src/advcomp.cc',
      'advancecomp/data.cc',
      'advancecomp/siglock.cc',
      'advancecomp/file.cc',
      'advancecomp/pngex.cc',
      'advancecomp/compress.cc',
      'advancecomp/lib/png.c',
      'advancecomp/lib/error.c',
      'advancecomp/lib/fz.c',
      'advancecomp/lib/snstring.c',
      'advancecomp/zopfli/zopfli_lib.c',
      'advancecomp/zopfli/deflate.c',
      'advancecomp/zopfli/lz77.c',
      'advancecomp/zopfli/blocksplitter.c',
      'advancecomp/zopfli/hash.c',
      'advancecomp/zopfli/cache.c',
      'advancecomp/zopfli/tree.c',
      'advancecomp/zopfli/gzip_container.c',
      'advancecomp/zopfli/util.c',
      'advancecomp/zopfli/squeeze.c',
      'advancecomp/zopfli/katajainen.c',
      'advancecomp/zopfli/zlib_container.c',
      'advancecomp/7z/7zdeflate.cc',
      'advancecomp/7z/InByte.cc',
      'advancecomp/7z/OutByte.cc',
      'advancecomp/7z/IInOutStreams.cc',
      'advancecomp/7z/WindowIn.cc',
      'advancecomp/7z/WindowOut.cc',
      'advancecomp/7z/DeflateDecoder.cc',
      'advancecomp/7z/DeflateEncoder.cc',
      'advancecomp/7z/HuffmanEncoder.cc',
      'advancecomp/7z/LSBFDecoder.cc',
      'advancecomp/7z/LSBFEncoder.cc',
      'advancecomp/7z/CRC.cc',
      'advancecomp/libdeflate/deflate_compress.c',
      'advancecomp/libdeflate/aligned_malloc.c',
      'advancecomp/libdeflate/zlib_compress.c',
      'advancecomp/libdeflate/adler32.c',
      'advancecomp/libdeflate/x86_cpu_features.c']
    include_dirs += [os.path.join(BASE_DIR, 'advancecomp')]

if WITH_MC_OPNG:
    defines += [
      ('PYOPTIPNG_WITH_MC_OPNG', None),
      ]
    all_sources += ['src/mc_opng.cc',
      'libpng/png.c',
      'libpng/pngread.c',
      'libpng/pngwrite.c',
      'libpng/pngerror.c',
      'libpng/pngrutil.c',
      'libpng/pngmem.c',
      'libpng/pngwutil.c',
      'libpng/pngtrans.c',
      'libpng/pngrtran.c',
      'libpng/pngwtran.c',
      'libpng/pngwio.c',
      'libpng/pngget.c',
      'libpng/pngrio.c',
      'libpng/pngset.c',
      'zlib/inflate.c',
      'zlib/zutil.c',
      'zlib/inffast.c',
      # 'zlib/contrib/inflate86/inffast.S',
      'zlib/inftrees.c',
      ]
    include_dirs += [
      os.path.join(BASE_DIR, 'src'),
      os.path.join(BASE_DIR, 'libpng'),
      os.path.join(BASE_DIR, 'zlib'),
      ]

pyoptipng_module = Extension('pyoptipng/_pyoptipng',
                                libraries=libraries,
                                extra_compile_args = ["-O3"],
                                sources=all_sources,
                                include_dirs=include_dirs,
                                undef_macros=['NDEBUG'],
                                define_macros=defines)

class my_build_ext(build_ext.build_ext):

    def build_extensions(self):
        # for ext in self.extensions:
        #     if ext.name == 'pyoptipng/_pyoptipng':
        #         for src in reversed(ext.sources):
        #           if '.S' in src:
        #             print src
        #             del ext.sources[ext.sources.index(src)]
        #             cmd = ['gcc', '-DUSE_MMX', '-o', src.replace('.S', '.o'), '-c', src]
        #             subprocess.Popen(cmd).wait()
        #             ext.extra_objects.append(src.replace('.S', '.o'))
        build_ext.build_ext.build_extensions(self)

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
          cmdclass={'build_ext': my_build_ext},
          packages=['pyoptipng'],
          ext_modules=[pyoptipng_module],
          include_package_data=True,
          zip_safe=False)
