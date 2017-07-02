#include <Python.h>

PyObject* compress_png(PyObject *self, PyObject *args);
PyObject* advpng(PyObject *self, PyObject *args);

//-----------------------------------------------------------------------------
static PyMethodDef pyoptipng_methods[] = {
    {
        "compress_png",
        compress_png,
        METH_VARARGS,
        "compress PNG file"
    },
    {
        "advpng",
        advpng,
        METH_VARARGS,
        "recompress PNG file"
    },
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