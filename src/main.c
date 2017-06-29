#include <Python.h>
#include <stdio.h>

#include <optipng.h>
#include <optim.c>

#define BUFFER_GRANULARITY  64*1024

typedef struct {
    int32_t size;
    int32_t pos;
    void *data;
} Stream;

static int start_of_line;

static FILE *con_file;
static FILE *log_file;

static void
app_printf(const char *fmt, ...)
{
    va_list arg_ptr;

    if (fmt[0] == 0)
        return;
    start_of_line = (fmt[strlen(fmt) - 1] == '\n') ? 1 : 0;

    if (con_file != NULL)
    {
        va_start(arg_ptr, fmt);
        vfprintf(con_file, fmt, arg_ptr);
        va_end(arg_ptr);
    }
    if (log_file != NULL)
    {
        va_start(arg_ptr, fmt);
        vfprintf(log_file, fmt, arg_ptr);
        va_end(arg_ptr);
    }
}

static void
app_print_cntrl(int cntrl_code)
{
    printf("%c", cntrl_code);
}

static void
app_progress(unsigned long current_step, unsigned long total_steps)
{
    printf("PROGRESS: %d/%d\n", current_step, total_steps);
}

static void
panic(const char *msg)
{
    printf("panic: %s\n", msg);
}

static void
opng_read_buffer_data(png_structp png_ptr, png_bytep data, size_t length)
{
    png_voidp io_ptr = png_get_io_ptr(png_ptr);
    int io_state = pngx_get_io_state(png_ptr);
    int io_state_loc = io_state & PNGX_IO_MASK_LOC;
    png_bytep chunk_sig;

    if(io_ptr == NULL)
        return;

    Stream* input_stream = (Stream*)io_ptr;
    memcpy((png_byte*)data, input_stream->data+input_stream->pos, (size_t)length);
    input_stream->pos += length;

    if (process.in_file_size == 0)  /* first piece of PNG data */
    {
        OPNG_ENSURE(length == 8, "PNG I/O must start with the first 8 bytes");
        process.in_datastream_offset = input_stream->pos - 8;
        process.status |= INPUT_HAS_PNG_DATASTREAM;
        if (io_state_loc == PNGX_IO_SIGNATURE)
            process.status |= INPUT_HAS_PNG_SIGNATURE;
        if (process.in_datastream_offset == 0)
            process.status |= INPUT_IS_PNG_FILE;
        else if (process.in_datastream_offset < 0)
            png_error(png_ptr,
                "Can't get the file-position indicator in input file");
        process.in_file_size = (osys_fsize_t)process.in_datastream_offset;
    }
    process.in_file_size += length;

    OPNG_ENSURE((io_state & PNGX_IO_READING) && (io_state_loc != 0),
                "Incorrect info in png_ptr->io_state");
    if (io_state_loc == PNGX_IO_CHUNK_HDR)
    {
        /* In libpng 1.4.x and later, the chunk length and the chunk name
         * are serialized in a single operation. This is also ensured by
         * the opngio add-on for libpng 1.2.x and earlier.
         */
        OPNG_ENSURE(length == 8, "Reading chunk header, expecting 8 bytes");
        chunk_sig = data + 4;

        if (memcmp(chunk_sig, sig_IDAT, 4) == 0)
        {
            OPNG_ENSURE(png_ptr == read_ptr, "Incorrect I/O handler setup");
            if (png_get_rows(read_ptr, read_info_ptr) == NULL)  /* 1st IDAT */
            {
                OPNG_ENSURE(process.in_idat_size == 0,
                            "Found IDAT with no rows");
                /* Allocate the rows here, bypassing libpng.
                 * This allows to initialize the contents and perform recovery
                 * in case of a premature EOF.
                 */
                if (png_get_image_height(read_ptr, read_info_ptr) == 0)
                    return;  /* premature IDAT; an error will occur later */
                OPNG_ENSURE(pngx_malloc_rows(read_ptr,
                                             read_info_ptr, 0) != NULL,
                            "Failed allocation of image rows; "
                            "unsafe libpng allocator");
                png_data_freer(read_ptr, read_info_ptr,
                               PNG_USER_WILL_FREE_DATA, PNG_FREE_ROWS);
            }
            else
            {
                /* There is split IDAT overhead. Join IDATs. */
                process.status |= INPUT_HAS_JUNK;
            }
            process.in_idat_size += png_get_uint_32(data);
        }
        else if (memcmp(chunk_sig, sig_PLTE, 4) == 0 ||
                 memcmp(chunk_sig, sig_tRNS, 4) == 0)
        {
            /* Add the chunk overhead (header + CRC) to the data size. */
            process.in_plte_trns_size += png_get_uint_32(data) + 12;
        }
        else
            opng_handle_chunk(png_ptr, chunk_sig);
    }
    else if (io_state_loc == PNGX_IO_CHUNK_CRC)
    {
        OPNG_ENSURE(length == 4, "Reading chunk CRC, expecting 4 bytes");
    }
}

static int
_sig_is_png(png_structp png_ptr,
                png_bytep sig, size_t sig_size,
                png_const_charpp fmt_name_ptr,
                png_const_charpp fmt_long_name_ptr)
{
   /* The signature of this function differs from the other pngx_sig_is_X()
    * functions, to allow extra functionality (e.g. customized error messages)
    * without requiring a full pngx_read_png().
    */

   static const char pngx_png_standalone_fmt_name[] =
      "PNG";
   static const char pngx_png_datastream_fmt_name[] =
      "PNG datastream";
   static const char pngx_png_standalone_fmt_long_name[] =
      "Portable Network Graphics";
   static const char pngx_png_datastream_fmt_long_name[] =
      "Portable Network Graphics embedded datastream";

   static const png_byte png_file_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
   static const png_byte mng_file_sig[8] = {138, 77, 78, 71, 13, 10, 26, 10};
   static const png_byte png_ihdr_sig[8] = {0, 0, 0, 13, 73, 72, 68, 82};

   int has_png_sig;

   /* Since png_read_png() fails rather abruptly with png_error(),
    * spend a little more effort to ensure that the format is indeed PNG.
    * Among other things, look for the presence of IHDR.
    */
   if (sig_size <= 25 + 18)  /* size of (IHDR + IDAT) > (12+13) + (12+6) */
      return -1;
   has_png_sig = (memcmp(sig, png_file_sig, 8) == 0);
   if (memcmp(sig + (has_png_sig ? 8 : 0), png_ihdr_sig, 8) != 0)
   {
      /* This is not valid PNG: get as much information as possible. */
      if (memcmp(sig, png_file_sig, 4) == 0 && (sig[4] == 10 || sig[4] == 13))
         png_error(png_ptr,
            "PNG file appears to be corrupted by text file conversions");
      else if (memcmp(sig, mng_file_sig, 8) == 0)
         png_error(png_ptr, "MNG decoding is not supported");
      /* JNG is handled by the pngxrjpg module. */
      return 0;  /* not PNG */
   }

   /* Store the format name. */
   if (fmt_name_ptr != NULL)
   {
      *fmt_name_ptr = has_png_sig ?
         pngx_png_standalone_fmt_name :
         pngx_png_datastream_fmt_name;
   }
   if (fmt_long_name_ptr != NULL)
   {
      *fmt_long_name_ptr = has_png_sig ?
         pngx_png_standalone_fmt_long_name :
         pngx_png_datastream_fmt_long_name;
   }
   return 1;  /* PNG, really! */
}

static void
my_opng_read_stream(Stream* input_stream)
{
    const char *fmt_name;
    int num_img;
    png_uint_32 reductions;
    const char * volatile err_msg;  /* volatile is required by cexcept */

    png_byte sig[128];
    size_t num;

    Try
    {
        read_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, opng_error, opng_warning);
        read_info_ptr = png_create_info_struct(read_ptr);
        if (read_info_ptr == NULL)
            Throw "Out of memory";

        /* Override the default libpng settings. */
        png_set_keep_unknown_chunks(read_ptr, PNG_HANDLE_CHUNK_ALWAYS, NULL, 0);
        png_set_user_limits(read_ptr, PNG_UINT_31_MAX, PNG_UINT_31_MAX);

        /* Read the input image file. */
        opng_init_read_data();
        pngx_set_read_fn(read_ptr, input_stream, opng_read_buffer_data);
        fmt_name = NULL;

        memcpy(sig, input_stream->data, sizeof(sig));
        num = sizeof(sig);

        if (_sig_is_png(read_ptr, sig, num, &fmt_name, NULL) > 0)
        {
            png_read_png(read_ptr, read_info_ptr, 0, NULL);
            num_img = 1;
        } else {
            printf("It's not PNG file\n");
            num_img = 0;
        }

        process.in_file_size = input_stream->size;

        if (num_img <= 0)
            Throw "Unrecognized image file format";
        if (num_img > 1)
            process.status |= INPUT_HAS_MULTIPLE_IMAGES;
        if ((process.status & INPUT_IS_PNG_FILE) &&
            (process.status & INPUT_HAS_MULTIPLE_IMAGES))
        {
            /* pngxtern can't distinguish between APNG and proper PNG. */
            fmt_name = (process.status & INPUT_HAS_PNG_SIGNATURE) ?
                       "APNG" : "APNG datastream";
        }
        OPNG_ENSURE(fmt_name != NULL, "No format name from pngxtern");

        printf("fmt_name: %s\n", fmt_name);

        if (process.in_file_size == 0)
        {
            opng_print_warning("Can't get the correct file size");
        }

        err_msg = NULL;  /* everything is ok */
    }
    Catch (err_msg)
    {
        /* If the critical info has been loaded, treat all errors as warnings.
         * This enables a more advanced data recovery.
         */
        if (opng_validate_image(read_ptr, read_info_ptr))
        {
           png_warning(read_ptr, err_msg);
           err_msg = NULL;
        }
    }

    Try
    {
        if (err_msg != NULL)
            Throw err_msg;

        /* Display format and image information. */
        if (strcmp(fmt_name, "PNG") != 0)
        {
            usr_printf("Importing %s", fmt_name);
            if (process.status & INPUT_HAS_MULTIPLE_IMAGES)
            {
                if (!(process.status & INPUT_IS_PNG_FILE))
                    usr_printf(" (multi-image or animation)");
                if (options.snip)
                    usr_printf("; snipping...");
            }
            usr_printf("\n");
        }
        opng_load_image_info(read_ptr, read_info_ptr, 1);
        opng_print_image_info(1, 1, 1, 1);
        usr_printf("\n");

        /* Choose the applicable image reductions. */
        reductions = OPNG_REDUCE_ALL & ~OPNG_REDUCE_METADATA;
        if (options.nb)
            reductions &= ~OPNG_REDUCE_BIT_DEPTH;
        if (options.nc)
            reductions &= ~OPNG_REDUCE_COLOR_TYPE;
        if (options.np)
            reductions &= ~OPNG_REDUCE_PALETTE;
        if (options.nz && (process.status & INPUT_HAS_PNG_DATASTREAM))
        {
            /* Do not reduce files with PNG datastreams under -nz. */
            reductions = OPNG_REDUCE_NONE;
        }
        if (process.status & INPUT_HAS_DIGITAL_SIGNATURE)
        {
            /* Do not reduce signed files. */
            reductions = OPNG_REDUCE_NONE;
        }
        if ((process.status & INPUT_IS_PNG_FILE) &&
            (process.status & INPUT_HAS_MULTIPLE_IMAGES) &&
            (reductions != OPNG_REDUCE_NONE) && !options.snip)
        {
            usr_printf(
                "Can't reliably reduce APNG file; disabling reductions.\n"
                "(Did you want to -snip and optimize the first frame?)\n");
            reductions = OPNG_REDUCE_NONE;
        }

        /* Try to reduce the image. */
        process.reductions =
            opng_reduce_image(read_ptr, read_info_ptr, reductions);

        /* If the image is reduced, enforce full compression. */
        if (process.reductions != OPNG_REDUCE_NONE)
        {
            opng_load_image_info(read_ptr, read_info_ptr, 1);
            usr_printf("Reducing image to ");
            opng_print_image_info(0, 1, 1, 0);
            usr_printf("\n");
        }

        /* Change the interlace type if required. */
        if (options.interlace >= 0 &&
            image.interlace_type != options.interlace)
        {
            image.interlace_type = options.interlace;
            /* A change in interlacing requires IDAT recoding. */
            process.status |= OUTPUT_NEEDS_NEW_IDAT;
        }
    }
    Catch (err_msg)
    {
        /* Do the cleanup, then rethrow the exception. */
        png_data_freer(read_ptr, read_info_ptr,
                       PNG_DESTROY_WILL_FREE_DATA, PNG_FREE_ALL);
        png_destroy_read_struct(&read_ptr, &read_info_ptr, NULL);
        Throw err_msg;
    }

    /* Destroy the libpng structures, but leave the enclosed data intact
     * to allow further processing.
     */
    png_data_freer(read_ptr, read_info_ptr,
                   PNG_USER_WILL_FREE_DATA, PNG_FREE_ALL);
    png_destroy_read_struct(&read_ptr, &read_info_ptr, NULL);
}

static void
my_opng_write_stream_data(png_structp png_ptr, png_bytep data, size_t length)
{
    png_voidp io_ptr = png_get_io_ptr(png_ptr);
    if(io_ptr == NULL)
        return;

    Stream* stream = (Stream*)io_ptr;

    while (stream->pos + length > stream->size) {
        stream->data = realloc((void*)stream->data, stream->size + BUFFER_GRANULARITY);
        stream->size += BUFFER_GRANULARITY;
    }

    memcpy(stream->data+stream->pos, data, (size_t)length);
    stream-> pos += length;

    process.out_file_size += length;
}

static void
my_opng_write_stream(Stream *stream, int compression_level, int memory_level, 
                   int compression_strategy, int filter)
{
    const char * volatile err_msg;  /* volatile is required by cexcept */

    OPNG_ENSURE(
        compression_level >= OPNG_COMPR_LEVEL_MIN &&
        compression_level <= OPNG_COMPR_LEVEL_MAX &&
        memory_level >= OPNG_MEM_LEVEL_MIN &&
        memory_level <= OPNG_MEM_LEVEL_MAX &&
        compression_strategy >= OPNG_STRATEGY_MIN &&
        compression_strategy <= OPNG_STRATEGY_MAX &&
        filter >= OPNG_FILTER_MIN &&
        filter <= OPNG_FILTER_MAX,
        "Invalid encoding parameters");

    Try
    {
        write_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
            NULL, opng_error, opng_warning);
        write_info_ptr = png_create_info_struct(write_ptr);
        if (write_info_ptr == NULL)
            Throw "Out of memory";

        png_set_compression_level(write_ptr, compression_level);
        png_set_compression_mem_level(write_ptr, memory_level);
        png_set_compression_strategy(write_ptr, compression_strategy);
        png_set_filter(write_ptr, PNG_FILTER_TYPE_BASE, filter_table[filter]);
        if (compression_strategy != Z_HUFFMAN_ONLY &&
            compression_strategy != Z_RLE)
        {
            if (options.window_bits > 0)
                png_set_compression_window_bits(write_ptr,
                                                options.window_bits);
        }
        else
        {
#ifdef WBITS_8_OK
            png_set_compression_window_bits(write_ptr, 8);
#else
            png_set_compression_window_bits(write_ptr, 9);
#endif
        }

        /* Override the default libpng settings. */
        png_set_keep_unknown_chunks(write_ptr,
                                    PNG_HANDLE_CHUNK_ALWAYS, NULL, 0);
        png_set_user_limits(write_ptr, PNG_UINT_31_MAX, PNG_UINT_31_MAX);

        /* Write the PNG stream. */
        opng_store_image_info(write_ptr, write_info_ptr, (stream != NULL));
        opng_init_write_data();
        pngx_set_write_fn(write_ptr, stream, my_opng_write_stream_data, NULL);
        png_write_png(write_ptr, write_info_ptr, 0, NULL);

        err_msg = NULL;  /* everything is ok */
    }
    Catch (err_msg)
    {
        /* Set IDAT size to invalid. */
        process.out_idat_size = idat_size_max + 1;
    }

    /* Destroy the libpng structures. */
    png_destroy_write_struct(&write_ptr, &write_info_ptr);

    if (err_msg != NULL)
        Throw err_msg;
}

int
my_opng_optimize(Stream* input, Stream* output)
{
    int result;
    const char *err_msg;

    opng_clear_image_info();
    Try
    {
        memset(&process, 0, sizeof(process));
        if (options.force)
            process.status |= OUTPUT_NEEDS_NEW_IDAT;

        err_msg = NULL;  /* prepare for error handling */

        Try
        {
            my_opng_read_stream(input);
        }
        Catch (err_msg)
        {
            OPNG_ENSURE(err_msg != NULL, "Mysterious error in opng_read_file");
        }
        // fclose(infile);  /* finally */
        // if (err_msg != NULL)
        //     Throw err_msg;  /* rethrow */

        /* Check the error flag. This must be the first check. */
        if (process.status & INPUT_HAS_ERRORS)
        {
            usr_printf("Recoverable errors found in input.");
            if (options.fix)
            {
                usr_printf(" Fixing...\n");
                process.status |= OUTPUT_NEEDS_NEW_FILE;
            }
            else
            {
                usr_printf(" Rerun " PROGRAM_NAME " with -fix enabled.\n");
                Throw "Previous error(s) not fixed";
            }
        }

        /* Check the junk flag. */
        if (process.status & INPUT_HAS_JUNK)
            process.status |= OUTPUT_NEEDS_NEW_FILE;

        /* Check the PNG signature and datastream flags. */
        if (!(process.status & INPUT_HAS_PNG_SIGNATURE))
            process.status |= OUTPUT_NEEDS_NEW_FILE;
        if (process.status & INPUT_HAS_PNG_DATASTREAM)
        {
            if (options.nz && (process.status & OUTPUT_NEEDS_NEW_IDAT))
            {
                usr_printf(
                    "IDAT recoding is necessary, but is disabled by the user.\n");
                Throw "Can't continue";
            }
        }
        else
            process.status |= OUTPUT_NEEDS_NEW_IDAT;

        /* Check the digital signature flag. */
        if (process.status & INPUT_HAS_DIGITAL_SIGNATURE)
        {
            usr_printf("Digital signature found in input.");
            if (options.force)
            {
                usr_printf(" Erasing...\n");
                process.status |= OUTPUT_NEEDS_NEW_FILE;
            }
            else
            {
                usr_printf(" Rerun " PROGRAM_NAME " with -force enabled.\n");
                Throw "Can't optimize digitally-signed files";
            }
        }

        /* Check the multi-image flag. */
        if (process.status & INPUT_HAS_MULTIPLE_IMAGES)
        {
            if (!options.snip && !(process.status & INPUT_IS_PNG_FILE))
            {
                usr_printf("Conversion to PNG requires snipping. "
                           "Rerun " PROGRAM_NAME " with -snip enabled.\n");
                Throw "Incompatible input format";
            }
        }
        if ((process.status & INPUT_HAS_APNG) && options.snip)
            process.status |= OUTPUT_NEEDS_NEW_FILE;

        /* Check the stripped-data flag. */
        if (process.status & INPUT_HAS_STRIPPED_DATA)
            usr_printf("Stripping metadata...\n");

        /* Display the input IDAT/file sizes. */
        if (process.status & INPUT_HAS_PNG_DATASTREAM)
            usr_printf("Input IDAT size = %" OSYS_FSIZE_PRIu " bytes\n",
                       process.in_idat_size);
        usr_printf("Input file size = %" OSYS_FSIZE_PRIu " bytes\n",
                   process.in_file_size);

        /* Find the best parameters and see if it's worth recompressing. */
        if (!options.nz || (process.status & OUTPUT_NEEDS_NEW_IDAT))
        {
            opng_init_iterations();
            opng_iterate();
            opng_finish_iterations();
        }
        if (process.status & OUTPUT_NEEDS_NEW_IDAT)
        {
            process.status |= OUTPUT_NEEDS_NEW_FILE;
            opng_check_idat_size(process.best_idat_size);
        }

        /* Stop here? */
        if (!(process.status & OUTPUT_NEEDS_NEW_FILE))
        {
            usr_printf("\nFile is already optimized.\n");
        }
        if (options.simulate)
        {
            usr_printf("\nNo output: simulation mode.\n");
            return 0;
        }

        Try
        {
            if (process.status & OUTPUT_NEEDS_NEW_IDAT)
            {
                printf("OUTPUT_NEEDS_NEW_IDAT\n");
                /* Write a brand new PNG datastream to the output. */
                my_opng_write_stream(output,
                    process.best_compr_level, process.best_mem_level,
                    process.best_strategy, process.best_filter);
            }
            else
            {
            //     /* Copy the input PNG datastream to the output. */
            //     infile =
            //         fopen((new_outfile ? infile_name_local : bakfile_name), "rb");
            //     if (infile == NULL)
            //         Throw "Can't reopen the input file";
            //     Try
            //     {
            //         if (process.in_datastream_offset > 0 &&
            //             osys_fseeko(infile, process.in_datastream_offset,
            //                          SEEK_SET) != 0)
            //             Throw "Can't reposition the input file";
            //         process.best_idat_size = process.in_idat_size;
            //         opng_copy_file(infile, outfile);
            //     }
            //     Catch (err_msg)
            //     {
            //         OPNG_ENSURE(err_msg != NULL,
            //                     "Mysterious error in opng_copy_file");
            //     }
            //     fclose(infile);  /* finally */
            //     if (err_msg != NULL)
            //         Throw err_msg;  /* rethrow */
            }
        }
        Catch (err_msg)
        {
            Throw err_msg;  /* rethrow */
        }

        /* Display the output IDAT/file sizes. */
        usr_printf("\nOutput IDAT size = %" OSYS_FSIZE_PRIu " bytes",
                   process.out_idat_size);
        if (process.status & INPUT_HAS_PNG_DATASTREAM)
        {
            usr_printf(" (");
            opng_print_fsize_difference(process.in_idat_size,
                                        process.out_idat_size, 0);
            usr_printf(")");
        }
        usr_printf("\nOutput file size = %" OSYS_FSIZE_PRIu " bytes (",
                   process.out_file_size);
        opng_print_fsize_difference(process.in_file_size,
                                    process.out_file_size, 1);
        usr_printf(")\n");


        result = 0;
    }
    Catch (err_msg)
    {
        opng_print_error(err_msg);
        result = -1;
    }
    opng_destroy_image_info();

    return 0;
}

static PyObject* compress_png(PyObject *self, PyObject *args)
{
    struct opng_options options;
    struct opng_ui ui;

    Stream input_stream;
    input_stream.pos = 0;

    Stream output_stream;
    output_stream.data = malloc(BUFFER_GRANULARITY);
    output_stream.size = BUFFER_GRANULARITY;
    output_stream.pos = 0;

    if (!PyArg_ParseTuple(args, "s#", &input_stream.data, &input_stream.size))
        return NULL;

    con_file = stdout;

    ui.printf_fn      = app_printf;
    ui.print_cntrl_fn = app_print_cntrl;
    ui.progress_fn    = app_progress;
    ui.panic_fn       = panic;

    memset(&options, 0, sizeof(options));
    options.optim_level = -1;
    options.interlace = -1;

    if (opng_initialize(&options, &ui) != 0)
    {
        PyErr_SetString(PyExc_ValueError, "opng_initialize() error");
        return NULL;
    }

    if (my_opng_optimize(&input_stream, &output_stream) != 0)
    {
        PyErr_SetString(PyExc_ValueError, "my_opng_optimize() error");
        return NULL;
    }

    if (opng_finalize() != 0)
    {
        PyErr_SetString(PyExc_ValueError, "opng_finalize() error");
        return NULL;
    }

    PyObject* result = Py_BuildValue("s#", output_stream.data, output_stream.pos);
    free(output_stream.data);

    return result;
}

//-----------------------------------------------------------------------------
static PyMethodDef pyoptipng_methods[] = {
    {
        "compress_png",
        compress_png,
        METH_VARARGS,
        "compress PNG file"
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