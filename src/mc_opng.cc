#include <Python.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <png.h>
#include <zlib.h>
#include <math.h>
#include <sched.h>

#include <queue>

#include <cpuid.h>

#define BUFGRAN     256*1024

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
    unsigned char** image_rows;
    int row_bytes;
    png_colorp palette;
    int num_palette;
};

struct thread_result {
    unsigned char* data;
    unsigned long size;
};

int comp_level_set[] = { 9, 8, 7, 6, 5, 4, 3 };

#define COMP_LEVEL_SET_SIZE 2

int comp_strategy_set[] = {
    Z_DEFAULT_STRATEGY,
    Z_FILTERED,
    Z_HUFFMAN_ONLY,
    Z_RLE,
    Z_FIXED,
};

#define COMP_STRATEGY_SET_SIZE 3

int interlace_variants[] = {
    PNG_INTERLACE_NONE,
    PNG_INTERLACE_ADAM7,
    };

#define NUM_INTERLACE_VARIANTS  2

int filter_variants[] = {
    PNG_ALL_FILTERS,
    PNG_FILTER_SUB,
    PNG_FILTER_UP,
    PNG_FILTER_AVG,
    PNG_FILTER_PAETH,
    PNG_FILTER_NONE,
    PNG_FILTER_SUB | PNG_FILTER_UP,
    PNG_FILTER_SUB | PNG_FILTER_AVG,
    PNG_FILTER_SUB | PNG_FILTER_PAETH,
    PNG_FILTER_UP | PNG_FILTER_AVG,
    PNG_FILTER_UP | PNG_FILTER_PAETH,
    PNG_FILTER_AVG | PNG_FILTER_PAETH,
    PNG_FILTER_SUB | PNG_FILTER_UP | PNG_FILTER_AVG,
    PNG_FILTER_SUB | PNG_FILTER_UP | PNG_FILTER_PAETH,
    PNG_FILTER_SUB | PNG_FILTER_AVG | PNG_FILTER_PAETH,
    };

#define NUM_FILTER_VARIANTS 6

std::queue<job_info*> jobs;
pthread_mutex_t mutex;

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

    // unsigned char** image_rows = NULL;
    // int image_height = 0;
    
    while(1) {
        pthread_mutex_lock(&mutex);
            if (jobs.empty()) {
                pthread_mutex_unlock(&mutex);
                break;
            }
            job_info* job = jobs.front();
            jobs.pop();

        // if (image_rows == NULL) {
        //     image_height = job->image_height;
        //     image_rows = (unsigned char**)malloc(sizeof()*job->image_height);
        //     for(unsigned int y=0; y < job->image_height; y++) {
        //         image_rows[y] = (unsigned char*)malloc(job->row_bytes);
        //         memcpy(image_rows[y], job->image_rows[y], job->row_bytes);
        //     }
        // }
        pthread_mutex_unlock(&mutex);

        png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
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

        output.pos = 0;
        png_set_write_fn(png_ptr, &output, custom_write_png, NULL);

        png_set_IHDR(
            png_ptr,
            info_ptr,
            job->image_width,
            job->image_height,
            job->bit_depth,
            job->color_type,
            job->interlace,
            job->compression_type,
            PNG_FILTER_TYPE_DEFAULT
            );

        if (job->color_type == PNG_COLOR_TYPE_PALETTE) {
            png_set_PLTE(png_ptr, info_ptr, job->palette, 1<<job->bit_depth);
        }

        png_set_filter(
            png_ptr,
            0,
            job->filter_type
            );

        png_set_compression_level(
            png_ptr,
            job->compression_level
            );

        png_set_compression_strategy(
            png_ptr,
            job->compression_strategy
            );

        png_write_info(png_ptr, info_ptr);
        png_write_image(png_ptr, job->image_rows);
        png_write_end(png_ptr, NULL);

        // printf("T%d:%d: %d\n", info->num, cpu, output.pos);

        png_destroy_write_struct(&png_ptr, &info_ptr);

        if (result->data == NULL) {
            result->data = (unsigned char*)malloc(output.pos);
            result->size = output.pos;
            memcpy(result->data, output.data, output.pos);
        } else {
            if (output.pos < result->size) {
                result->data = (unsigned char*)realloc(result->data, output.pos);
                result->size = output.pos;
                memcpy(result->data, output.data, output.pos);
            }
        }

        free(job);
    }

    // for(unsigned int y=0; y<image_height; y++)
    // {
    //     free(image_rows[y]);
    // }
    // free(image_rows);

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

    png.pos = 0;

    if (!PyArg_ParseTuple(args, "s#|i", &png.data, &png.size, &optim_level))
        return NULL;

    int is_png = !png_sig_cmp((png_const_bytep)png.data, 0, 8);

    if(!is_png)
    {
        PyErr_SetString(PyExc_ValueError, "Not valid PNG file");
        return NULL;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
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
    png_read_info(png_ptr, info_ptr);

    int image_width = png_get_image_width(png_ptr, info_ptr);
    int image_height = png_get_image_height(png_ptr, info_ptr);
    int color_type = png_get_color_type(png_ptr, info_ptr);
    int bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    printf("PNG info: %dx%d %d %d", image_width, image_height, color_type, bit_depth);

    image_rows = (unsigned char**)malloc(sizeof(unsigned char*)*image_height);
    for(unsigned int y=0; y < image_height; y++) {
        image_rows[y] = (unsigned char*)malloc(png_get_rowbytes(png_ptr, info_ptr));
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        palette = (png_colorp)png_malloc(png_ptr, PNG_MAX_PALETTE_LENGTH*sizeof (png_color));
        png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);
        printf("num_palette=%d\n", num_palette);
    }

    png_read_image(png_ptr, image_rows);

    int num_cpu = sysconf(_SC_NPROCESSORS_ONLN);
    printf("CPU cores: %d\n", num_cpu);

    pthread_mutex_init(&mutex, NULL);

    printf("Creating jobs...");
    for(unsigned int i=0; i<NUM_INTERLACE_VARIANTS; i++) {
        for(unsigned int f=0; f<NUM_FILTER_VARIANTS; f++) {
            for(unsigned int cl=0; cl<COMP_LEVEL_SET_SIZE; cl++) {
                for(unsigned int st=0; st<COMP_STRATEGY_SET_SIZE; st++) {
                    job_info* job = (job_info*)malloc(sizeof(job_info));
                    memset(job, 0, sizeof(job_info));
                    job->image_width = image_width;
                    job->image_height = image_height;
                    job->bit_depth = bit_depth;
                    job->color_type = color_type;
                    job->interlace = interlace_variants[i];
                    job->compression_type = PNG_COMPRESSION_TYPE_DEFAULT;
                    job->compression_level = comp_level_set[cl];
                    job->compression_strategy = comp_strategy_set[st];
                    job->filter_type = filter_variants[f];
                    job->image_rows = image_rows;
                    job->row_bytes = png_get_rowbytes(png_ptr, info_ptr);
                    job->palette = palette;
                    job->num_palette = num_palette;
                    jobs.push(job);
                }
            }
        }
    }
    printf("DONE. %d jobs created.\n", jobs.size());

    threads = (thread_info**)malloc(sizeof(thread_info*)*num_cpu);

    printf("Creating threads...");
    for(unsigned int i=0; i<num_cpu; i++)
    {
        cpu_set_t cpuset;
        pthread_attr_t attr;

        pthread_attr_init(&attr);
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);

        threads[i] = (thread_info*)malloc(sizeof(thread_info));
        memset(threads[i], 0, sizeof(thread_info));
        threads[i]->num = i;
        pthread_create(&(threads[i]->id), &attr, worker, threads[i]);
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
            if (result->size < best_result->size) {
                free(best_result);
                best_result = result;
            } else {
                free(result);
            }
        } else {
            best_result = result;
        }
    }
    free(threads);
    printf("DONE.\n");

    printf("Best size: %d\n", best_result->size);

    PyObject* result = Py_BuildValue("s#", best_result->data, best_result->size);
    free(best_result->data);
    free(best_result);

    for(unsigned int y=0; y<image_height; y++)
    {
        free(image_rows[y]);
    }
    free(image_rows);
    // if (palette) {
    //     png_free(png_ptr, palette);
    // }

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return result;
}

}