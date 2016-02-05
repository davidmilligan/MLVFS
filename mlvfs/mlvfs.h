/*
 * Copyright (C) 2014 David Milligan
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef mlvfs_mlvfs_h
#define mlvfs_mlvfs_h

#include <stdio.h>
#include <sys/types.h>
#include <math.h>
#include <stdint.h>
#include "raw.h"
#include "mlv.h"

struct mlvfs
{
    char * mlv_path;
    char * port;
    int name_scheme;
    int chroma_smooth;
    int fix_bad_pixels;
    int fix_stripes;
    int dual_iso;
    int hdr_interpolation_method;
    int hdr_no_fullres;
    int hdr_no_alias_map;
    int prefetch;
    double fps;
    int deflicker;
    int fix_pattern_noise;
    int version;
};

//all the mlv block headers corresponding to a particular frame, needed to generate a DNG for that frame
struct frame_headers
{
    uint32_t fileNumber;
    uint64_t position;
    mlv_vidf_hdr_t vidf_hdr;
    mlv_file_hdr_t file_hdr;
    mlv_rtci_hdr_t rtci_hdr;
    mlv_idnt_hdr_t idnt_hdr;
    mlv_rawi_hdr_t rawi_hdr;
    mlv_expo_hdr_t expo_hdr;
    mlv_lens_hdr_t lens_hdr;
    mlv_wbal_hdr_t wbal_hdr;
};

#define MLVFS_SOFTWARE_NAME "MLVFS"

#ifndef VERSION
#define VERSION "UNKNOWN"
#endif
#ifndef BUILD_DATE
#define BUILD_DATE "UNKNOWN"
#endif

//Let the DNGs be "writeable" for AE, even though they're not actually writable
//You'll get an error if you actually try to write to them
#define ALLOW_WRITEABLE_DNGS

int string_ends_with(const char *source, const char *ending);
FILE** mlvfs_load_chunks(const char * path, uint32_t * chunk_count);
int mlv_get_frame_headers(const char *path, int index, struct frame_headers * frame_headers);
int mlv_get_frame_count(const char *real_path);
size_t get_image_data(struct frame_headers * frame_headers, FILE * file, uint8_t * output_buffer, off_t offset, size_t max_size);
#include <pthread.h>

//some macros for simple thread synchronization
#define CREATE_MUTEX(x) static pthread_mutex_t x = PTHREAD_MUTEX_INITIALIZER;
#define LOCK(x) static pthread_mutex_t x = PTHREAD_MUTEX_INITIALIZER; pthread_mutex_lock(&x);
#define RELOCK(x) pthread_mutex_lock(&(x));
#define UNLOCK(x) pthread_mutex_unlock(&(x));
#define CURRENT_THREAD (pthread_self())
#define THREAD_T pthread_t
#define LOCK_T pthread_mutex_t
#define INIT_LOCK(x) pthread_mutex_init(&(x), NULL)
#define DESTROY_LOCK(x) pthread_mutex_destroy(&(x))

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))
#define ABS(a) ((a) > 0 ? (a) : -(a))

#ifdef _WIN32
#define log2(x) log((float)(x))/log(2.)
#endif


#include "resource_manager.h"

int create_preview(struct image_buffer * image_buffer);

#endif
