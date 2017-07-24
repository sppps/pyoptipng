#include <Python.h>

#ifdef PYOPTIPNG_WITH_OPTIPNG
PyObject* compress_png(PyObject *self, PyObject *args);
#endif

#ifdef PYOPTIPNG_WITH_ADVANCECOMP
PyObject* advpng(PyObject *self, PyObject *args);
#endif

#ifdef PYOPTIPNG_WITH_MC_OPNG
PyObject* mc_compress_png(PyObject *self, PyObject *args);
#endif

//-----------------------------------------------------------------------------
static PyMethodDef pyoptipng_methods[] = {
#ifdef PYOPTIPNG_WITH_OPTIPNG
    {
        "compress_png",
        compress_png,
        METH_VARARGS,
        "compress PNG file"
    },
#endif
#ifdef PYOPTIPNG_WITH_ADVANCECOMP
    {
        "advpng",
        advpng,
        METH_VARARGS,
        "recompress PNG file"
    },
#endif
#ifdef PYOPTIPNG_WITH_MC_OPNG
    {
        "mc_compress_png",
        mc_compress_png,
        METH_VARARGS,
        "compress PNG file (multi-core version)"
    },
#endif
    {NULL, NULL, 0, NULL}
};

//-----------------------------------------------------------------------------
#if PY_MAJOR_VERSION < 3

PyMODINIT_FUNC init_pyoptipng(void)
{
    (void) Py_InitModule("_pyoptipng", pyoptipng_methods);
}

#else /* PY_MAJOR_VERSION >= 3 */

static struct PyModuleDef pyoptipng_module_def = {
    PyModuleDef_HEAD_INIT,
    "_pyoptipng",
    "\"_pyoptipng\" module",
    -1,
    pyoptipng_methods
};

PyMODINIT_FUNC PyInit__pyoptipng(void)
{
    return PyModule_Create(&pyoptipng_module_def);
}

#endif /* PY_MAJOR_VERSION >= 3 */