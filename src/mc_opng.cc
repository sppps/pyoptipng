#include <Python.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <png.h>
#include <zlib.h>
#include <sched.h>
#include <cpuid.h>
#include <opngreduc.h>

#include <queue>
#include <list>

#define BUFGRAN     256*1024

#ifdef __linux__
#define USE_PTHREAD_AFFINITY
#endif

#define CPUID(INFO, LEAF, SUBLEAF) __cpuid_count(LEAF, SUBLEAF, INFO[0], INFO[1], INFO[2], INFO[3])

#define GETCPU(CPU) {                              \
        uint32_t CPUInfo[4];                           \
        CPUID(CPUInfo, 1, 0);                          \
        /* CPUInfo[1] is EBX, bits 24-31 are APIC ID */ \
        if ( (CPUInfo[3] & (1 << 9)) == 0) {           \
          CPU = -1;  /* no APIC on chip */             \
        }                                              \
        else {                                         \
          CPU = (unsigned)CPUInfo[1] >> 24;                    \
        }                                              \
        if (CPU < 0) CPU = 0;                          \
      }

struct thread_info {
    int num;
    pthread_t id;
};

struct stream {
    unsigned char* data;
    unsigned long size;
    unsigned long pos;
};

struct job_info {
    int image_width;
    int image_height;
    int bit_depth;
    int color_type;
    int interlace;
    int compression_type;
    int filter_type;
    int compression_level;
    int compression_strategy;
    int compression_mem_level;
    unsigned char** image_rows;
    int row_bytes;
    png_colorp palette;
    int num_palette;
    png_color_16p background_ptr;
    png_bytep trans_alpha;
    int num_trans;
    png_color_16p trans_color_ptr;
    png_color_16 trans_color;
};

struct thread_result {
    unsigned char* data;
    unsigned long size;
};

struct optim_preset {
    const int m[10];
    const int c[10];
    const int s[5];
    const int f[7];
};

#define MAX_OPTIM_LEVEL 8

static const int filter_table[] =
{
    PNG_FILTER_NONE,
    PNG_FILTER_SUB,
    PNG_FILTER_UP,
    PNG_FILTER_AVG,
    PNG_FILTER_PAETH,
    PNG_ALL_FILTERS
};

static optim_preset presets[MAX_OPTIM_LEVEL+1] = {
    /*  Optimization level: 0 */ { 
        .m = { -1 },
        .c = { -1 },
        .s = { -1 },
        .f = { -1 }
        },
    /*  Optimization level: 1 */ { 
        .m = { -1 },
        .c = { -1 },
        .s = { -1 },
        .f = { -1 }
        },
    /*  Optimization level: 2 */ {
        .m = { 8, -1 },
        .c = { 9, -1 },
        .s = { 0, 1, 2, 3, -1 },
        .f = { 0, 5, -1 }
        },
    /*  Optimization level: 3 */ {
        .m = { 8, 9, -1 }, 
        .c = { 9, -1 },
        .s = { 0, 1, 2, 3, -1 },
        .f = { 0, 5, -1 }
        },
    /*  Optimization level: 4 */ {
        .m = { 8, -1 },
        .c = { 9, -1 },
        .s = { 0, 1, 2, 3, -1 },
        .f = { 0, 1, 2, 3, 4, 5, -1 }
        },
    /*  Optimization level: 5 */ {
        .m = { 8, 9, -1 },
        .c = { 9, -1 },
        .s = { 0, 1, 2, 3, -1 },
        .f = { 0, 1, 2, 3, 4, 5, -1 }
        },
    /*  Optimization level: 6 */ {
        .m = { 8, -1 },
        .c = { 5, 6, 7, 8, 9, -1 },
        .s = { 0, 1, 2, 3, -1 },
        .f = { 0, 1, 2, 3, 4, 5, -1 }
        },
    /*  Optimization level: 7 */ {
        .m = { 8, 9, -1 },
        .c = { 5, 6, 7, 8, 9, -1 },
        .s = { 0, 1, 2, 3, -1 },
        .f = { 0, 1, 2, 3, 4, 5, -1 }
        },
    /*  Optimization level: 8 */ {
        .m = { 1, 2, 3, 4, 5, 6, 7, 8, 9, -1 }, 
        .c = { 5, 6, 7, 8, 9, -1 },
        .s = { 0, 1, 2, 3, -1 },
        .f = { 0, 1, 2, 3, 4, 5, -1 }
        },
};

std::queue<job_info*> jobs;
pthread_mutex_t mutex;

void my_error_fn(png_structp png_ptr, png_const_charp error_msg){
    printf("PNG error: %s\n", error_msg);
}
void my_warning_fn(png_structp png_ptr, png_const_charp warning_msg){
    printf("PNG warning: %s\n", warning_msg);
}

static void custom_read_png(png_structp png_ptr, unsigned char* buf, unsigned long size) {
    stream* png_stream = (stream*)png_get_io_ptr(png_ptr);

    memcpy(buf, png_stream->data+png_stream->pos, size);
    png_stream->pos += size;
}

static void custom_write_png(png_structp png_ptr, unsigned char* buf, unsigned long size) {
    stream* png_stream = (stream*)png_get_io_ptr(png_ptr);

    while (png_stream->pos + size > png_stream->size) {
        png_stream->data = (unsigned char*)realloc(png_stream->data, png_stream->size + BUFGRAN);
        png_stream->size += BUFGRAN;
    }
    memcpy(png_stream->data+png_stream->pos, buf, size);
    png_stream->pos += size;
}

static void* worker(void *arg)
{
    thread_info* info = (thread_info*)arg;
    int cpu = 0;
    GETCPU(cpu);

    stream output;
    thread_result* result = (thread_result*)malloc(sizeof(thread_result));
    result->data = NULL;
    result->size = 0;

    output.data = (unsigned char *)malloc(BUFGRAN);
    output.size = BUFGRAN;
    output.pos = 0;

    printf("Thread %d on CPU %d\n", info->num, cpu);
    
    while(1) {
        pthread_mutex_lock(&mutex);
            if (jobs.empty()) {
                pthread_mutex_unlock(&mutex);
                break;
            }
            job_info* job = jobs.front();
            jobs.pop();
        pthread_mutex_unlock(&mutex);

        png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, my_error_fn, my_warning_fn);
        if (!png_ptr) {
            free(job);
            continue;
        }

        png_infop info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr) {
            png_destroy_write_struct(&png_ptr, NULL);
            free(job);
            continue;
        }

        png_set_compression_level(png_ptr, job->compression_level);
        png_set_compression_mem_level(png_ptr, job->compression_mem_level);
        png_set_compression_strategy(png_ptr, job->compression_strategy);
        png_set_filter(png_ptr, PNG_FILTER_TYPE_BASE, filter_table[job->filter_type]);
        // png_set_compression_window_bits(png_ptr, 15);

        output.pos = 0;
        png_set_write_fn(png_ptr, &output, custom_write_png, NULL);

        png_set_IHDR(png_ptr, info_ptr,
            job->image_width,
            job->image_height,
            job->bit_depth,
            job->color_type,
            job->interlace,
            job->compression_type,
            PNG_FILTER_TYPE_DEFAULT
            );

        png_set_user_limits(png_ptr, PNG_UINT_31_MAX, PNG_UINT_31_MAX);
        png_set_rows(png_ptr, info_ptr, job->image_rows);

        if (job->color_type == PNG_COLOR_TYPE_PALETTE) {
            png_set_PLTE(png_ptr, info_ptr, job->palette, 1<<job->bit_depth);
        }

        if (job->trans_alpha != NULL || job->trans_color_ptr != NULL)
            png_set_tRNS(png_ptr, info_ptr,
                job->trans_alpha, job->num_trans, job->trans_color_ptr);

        if (job->background_ptr != NULL)
            png_set_bKGD(png_ptr, info_ptr, job->background_ptr);

        png_write_png(png_ptr, info_ptr, 0, NULL);

        png_destroy_write_struct(&png_ptr, &info_ptr);

        printf("zc = %d, zm = %d, zs = %d, f = %d, size: %d\n",
            job->compression_level,
            job->compression_mem_level,
            job->compression_strategy,
            job->filter_type,
            output.pos);

        if (result->data == NULL) {
            result->data = (unsigned char*)malloc(output.pos);
            result->size = output.pos;
            memcpy(result->data, output.data, output.pos);
        } else {
            if (output.pos < result->size && output.pos > 0) {
                result->data = (unsigned char*)realloc(result->data, output.pos);
                result->size = output.pos;
                memcpy(result->data, output.data, output.pos);
            }
        }

        free(job);
    }

    free(output.data);

    return result;
}

extern "C" {

PyObject* mc_compress_png(PyObject *self, PyObject *args)
{
    stream png;
    int optim_level;
    unsigned char** image_rows = NULL;
    png_colorp palette = NULL;
    int num_palette;
    thread_info** threads = NULL;
    png_color_16p background_ptr = NULL;
    png_color_16 background;
    png_bytep trans_alpha = NULL;
    int num_trans;
    png_color_16p trans_color_ptr = NULL;
    png_color_16 trans_color;

    png.pos = 0;

    if (!PyArg_ParseTuple(args, "s#|i", &png.data, &png.size, &optim_level))
        return NULL;

    int is_png = !png_sig_cmp((png_const_bytep)png.data, 0, 8);

    if(!is_png)
    {
        PyErr_SetString(PyExc_ValueError, "Not valid PNG file");
        return NULL;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, my_error_fn, my_warning_fn);
    if (!png_ptr)
    {
        PyErr_SetString(PyExc_ValueError, "png_create_read_struct() error");
        return NULL;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        PyErr_SetString(PyExc_ValueError, "png_create_info_struct() error");
        return NULL;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        PyErr_SetString(PyExc_ValueError, "libpng error");
        return NULL;
    }

    png_set_read_fn(png_ptr, &png, custom_read_png);
    png_read_png(png_ptr, info_ptr, 0, NULL);

    int reduction = opng_reduce_image(png_ptr, info_ptr, OPNG_REDUCE_ALL & ~OPNG_REDUCE_METADATA);
    int image_width = png_get_image_width(png_ptr, info_ptr);
    int image_height = png_get_image_height(png_ptr, info_ptr);
    int color_type = png_get_color_type(png_ptr, info_ptr);
    int bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    int interlace_type = png_get_interlace_type(png_ptr, info_ptr);
    int compression_type = png_get_compression_type(png_ptr, info_ptr);

    printf("PNG info: %dx%d %d %d", image_width, image_height, color_type, bit_depth);

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        palette = (png_colorp)png_malloc(png_ptr, PNG_MAX_PALETTE_LENGTH*sizeof (png_color));
        png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);
        printf("num_palette=%d\n", num_palette);
    }

    if (png_get_tRNS(png_ptr, info_ptr, &trans_alpha, &num_trans, &trans_color_ptr))
    {
        if (trans_color_ptr != NULL)
        {
            trans_color = *trans_color_ptr;
            trans_color_ptr = &trans_color;
        }
    }

    if (png_get_bKGD(png_ptr, info_ptr, &background_ptr))
    {
        background = *background_ptr;
        background_ptr = &background;
    }

    image_rows = png_get_rows(png_ptr, info_ptr);

    int num_cpu = sysconf(_SC_NPROCESSORS_ONLN);
    printf("CPU cores: %d\n", num_cpu);

    printf("Creating jobs...");

    optim_preset* preset = &presets[optim_level];


    for(unsigned int m=0; preset->m[m] != -1; m++) {
        for(unsigned int f=0; preset->f[f] != -1; f++) {
            for(unsigned int c=0; preset->c[c] != -1; c++) {
                for(unsigned int s=0; preset->s[s] != -1; s++) {
                    job_info* job = (job_info*)malloc(sizeof(job_info));
                    memset(job, 0, sizeof(job_info));
                    job->image_width = image_width;
                    job->image_height = image_height;
                    job->bit_depth = bit_depth;
                    job->color_type = color_type;
                    job->interlace = interlace_type;
                    job->compression_mem_level = preset->m[m];
                    job->compression_type = compression_type;
                    job->compression_level = preset->c[c];
                    job->compression_strategy = preset->s[s];
                    job->filter_type = preset->f[f];
                    job->image_rows = image_rows;
                    job->row_bytes = png_get_rowbytes(png_ptr, info_ptr);
                    job->palette = palette;
                    job->num_palette = num_palette;
                    job->background_ptr = background_ptr;
                    job->trans_alpha = trans_alpha;
                    job->num_trans = num_trans;
                    job->trans_color_ptr = trans_color_ptr;
                    job->trans_color = trans_color;
                    jobs.push(job);
                }
            }
        }
    }
    printf("DONE. %d jobs created.\n", jobs.size());

    threads = (thread_info**)malloc(sizeof(thread_info*)*num_cpu);
    pthread_mutex_init(&mutex, NULL);

    printf("Creating threads...");
    for(unsigned int i=0; i<num_cpu; i++)
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);

        #ifdef USE_PTHREAD_AFFINITY
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
        #endif

        threads[i] = (thread_info*)malloc(sizeof(thread_info));
        memset(threads[i], 0, sizeof(thread_info));
        threads[i]->num = i;
        pthread_create(&(threads[i]->id), &attr, worker, threads[i]);
        pthread_attr_destroy(&attr);
    }
    printf("DONE.\n");

    printf("Waiting for threads...");
    thread_result* best_result = NULL;
    for(unsigned int i=0; i<num_cpu; i++)
    {
        thread_result* result;
        pthread_join(threads[i]->id, (void**)&result);
        free(threads[i]);
        if (best_result) {
            if (result->size < best_result->size && result->size > 0) {
                free(best_result->data);
                free(best_result);
                best_result = result;
            } else {
                free(result->data);
                free(result);
            }
        } else {
            if (result->size > 0) {
                best_result = result;
            } else {
                free(result->data);
                free(result);
            }
        }
    }
    free(threads);
    pthread_mutex_destroy(&mutex);
    printf("DONE.\n");

    printf("Best size: %d\n", best_result->size);

    PyObject* result = Py_BuildValue("s#", best_result->data, best_result->size);
    free(best_result->data);
    free(best_result);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return result;
}

}