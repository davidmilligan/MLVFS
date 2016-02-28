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

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/param.h>
#include <unistd.h>
#include <wordexp.h>
#endif
#include <stddef.h>
#include <fuse.h>
#include "raw.h"
#include "mlv.h"
#include "dng.h"
#include "index.h"
#include "wav.h"
#include "stripes.h"
#include "cs.h"
#include "hdr.h"
#include "webgui.h"
#include "resource_manager.h"
#include "mlvfs.h"
#include "LZMA/LzmaLib.h"
#include "lj92.h"
#include "gif.h"
#include "histogram.h"
#include "patternnoise.h"

static struct mlvfs mlvfs;

#ifdef _WIN32

#include <io.h>
#include <direct.h>

int pread(int fh, void *buf, size_t size, long offset)
{
    off_t pos = lseek(fh, 0, SEEK_CUR);
    lseek(fh, offset, SEEK_SET);
    int res = read(fh, buf, (unsigned int)size);
    lseek(fh, pos, SEEK_SET);

    return res;
}

int pwrite(int fh, void *buf, size_t size, long offset)
{
    off_t pos = lseek(fh, 0, SEEK_CUR);
    lseek(fh, offset, SEEK_SET);
    int res = write(fh, buf, (unsigned int)size);
    lseek(fh, pos, SEEK_SET);

    return res;
}

int truncate(char *file, size_t length)
{
    LARGE_INTEGER large;
    large.QuadPart = length;

    HANDLE fh = CreateFile(file, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

    if (fh == INVALID_HANDLE_VALUE)
    {
        return 1;
    }

    if (SetFilePointerEx(fh, large, NULL, FILE_BEGIN) == 0 || SetEndOfFile(fh) == 0)
    {
        CloseHandle(fh);
        return 1;
    }

    CloseHandle(fh);
    return 0;
}

/* visual studio compilers for win32 offer a C exception handling. make use of it to reduce risk of severe crashes. */
#define TRY_WRAP(code) __try { code } __except(ExceptionFilter(__FUNCTION__,__LINE__,GetExceptionCode(), GetExceptionInformation())) { return -ENOENT; }

int ExceptionFilter(const char *func, unsigned int line, unsigned int code, struct _EXCEPTION_POINTERS *info)
{
    fprintf(stderr, "%s(%d): caught exception 0x%08X at address 0x%08llX\n", func, line, code, (uint64_t)info->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

static timestruc_t timeToTimestruct(time_t t)
{
    timestruc_t ts;
    
    ts.tv_nsec = 0;
    ts.tv_sec = t;
    
    return ts;
}

#else

#define TRY_WRAP(code) code

#endif


double * get_raw2evf(int black)
{
    static int initialized = 0;
    static double raw2ev_base[16384 + MAX_BLACK];
    
    if(!initialized)
    {
        memset(raw2ev_base, 0, MAX_BLACK * sizeof(int));
        int i;
        for (i = 0; i < 16384; i++)
        {
            raw2ev_base[i + MAX_BLACK] = log2(i) * EV_RESOLUTION;
        }
        initialized = 1;
    }
    
    if(black > MAX_BLACK)
    {
        err_printf("Black level too large for processing\n");
        return NULL;
    }
    double * raw2ev = &(raw2ev_base[MAX_BLACK - black]);
    
    return raw2ev;
}

int * get_raw2ev(int black)
{
    
    static int initialized = 0;
    static int raw2ev_base[16384 + MAX_BLACK];
    
    if(!initialized)
    {
        memset(raw2ev_base, 0, MAX_BLACK * sizeof(int));
        int i;
        for (i = 0; i < 16384; i++)
        {
            raw2ev_base[i + MAX_BLACK] = (int)(log2(i) * EV_RESOLUTION);
        }
        initialized = 1;
    }
    
    if(black > MAX_BLACK)
    {
        err_printf("Black level too large for processing\n");
        return NULL;
    }
    int * raw2ev = &(raw2ev_base[MAX_BLACK - black]);
    
    return raw2ev;
}

int * get_ev2raw()
{
    static int initialized = 0;
    static int _ev2raw[24*EV_RESOLUTION];
    int* ev2raw = _ev2raw + 10*EV_RESOLUTION;
    if(!initialized)
    {
        int i;
        for (i = -10*EV_RESOLUTION; i < 14*EV_RESOLUTION; i++)
        {
            ev2raw[i] = (int)(pow(2, (float)i / EV_RESOLUTION));
        }
        initialized = 1;
    }
    return ev2raw;
}

/**
 * Determines if a string ends in some string
 */
int string_ends_with(const char *source, const char *ending)
{
    if(source == NULL || ending == NULL) return 0;
    if(strlen(source) <= 0) return 0;
    if(strlen(source) < strlen(ending)) return 0;
    return !filename_strcmp(source + strlen(source) - strlen(ending), ending);
}

/**
 * Make sure you free() the result!!!
 */
static char * copy_string(const char * source)
{
    size_t size = strlen(source) + 1;
    char *copy = malloc(sizeof(char) * size);
    if(copy)
    {
        strncpy(copy, source, size);
    }
    else
    {
        int err = errno;
        err_printf("malloc error: %s\n", strerror(err));
    }
    return copy;
}

/**
 * Make sure you free() the result!!!
 */
static char * concat_string(const char * str1, const char * str2)
{
    size_t size = strlen(str1) + strlen(str2) + 1;
    char *copy = malloc(sizeof(char) * size);
    if(copy)
    {
        sprintf(copy, "%s%s", str1, str2);
    }
    else
    {
        int err = errno;
        err_printf("malloc error: %s\n", strerror(err));
    }
    return copy;
}

/**
 * Make sure you free() the result!!!
 */
static char * concat_string3(const char * str1, const char * str2, const char * str3)
{
    size_t size = strlen(str1) + strlen(str2) + strlen(str3) + 1;
    char *copy = malloc(sizeof(char) * size);
    if(copy)
    {
        sprintf(copy, "%s%s%s", str1, str2, str3);
    }
    else
    {
        int err = errno;
        err_printf("malloc error: %s\n", strerror(err));
    }
    return copy;
}

static char * path_append(const char * path1, const char * path2)
{
	if (path1 == NULL || path2 == NULL) return NULL;
	if (string_ends_with(path1, "/") || string_ends_with(path1, "\\"))
	{
		if (path2[0] == '/' || path2[0] == '\\')
		{
			return concat_string(path1, path2 + 1);
		}
		else
		{
			return concat_string(path1, path2);
		}
	}
	else
	{
		if (path2[0] == '/' || path2[0] == '\\')
		{
			return concat_string(path1, path2);
		}
		else
		{
			return concat_string3(path1, DIR_SEP_STR, path2);
		}
	}
}

static char *path_slashfix(char *path)
{
    char *pos = path;

    while (pos)
    {
        pos = find_first_separator(pos);

        if (pos)
        {
            *pos = DIR_SEP_CHAR;
            pos++;
        }
    }

    return path;
}

/**
 * Determines the real path to an MLV file based on a virtual path from FUSE
 * @param path The virtual path within the FUSE filesystem
 * @param mlv_filename [out] The real path to the MLV file (Make sure you free() the result!!!)
 * @return 1 if apparently within an MLV file, 0 otherwise
 */
static int get_mlv_filename(const char *path, char ** mlv_filename)
{
    if(strstr(path,"/._")) return 0;
    int result = 0;
	char * found = NULL;
    if(string_ends_with(path, ".MLV") || string_ends_with(path, ".mlv"))
    {
        *mlv_filename = path_append(mlvfs.mlv_path, path);
        result = 1;
    }
	else if ((found = lookup_mlv_name(path)) != NULL)
	{
		*mlv_filename = copy_string(found);
		result = 1;
	}
    else
    {
        *mlv_filename = NULL;
        char *temp = copy_string(path);
        char *split;
        if((split = find_last_separator(temp + 1)) != NULL)
        {
            *split = 0x00;
            if(mlvfs.name_scheme)
            {
                char * found = lookup_mlv_name(temp);
                if(found)
                {
                    *mlv_filename = copy_string(found);
                    result = 1;
                }
            }
            else
            {
                *mlv_filename = path_append(mlvfs.mlv_path, temp);
                result = string_ends_with(temp, ".MLV") || string_ends_with(temp, ".mlv");
            }
        }
        free(temp);
    }
    return result;
}

/**
 * Parse the frame number out of a file path and return it as an integer
 * @param path The virtual file path of the DNG
 * @return The frame number for that DNG
 */
static int get_mlv_frame_number(const char *path)
{
    int result = 0;
    char *temp = copy_string(path);
    char *dot = strrchr(temp, '.');
    if(dot > temp + 6)
    {
        *dot = '\0';
        result = atoi(dot - 6);
    }
    free(temp);
    return result;
}

/**
 * Make sure you free() the result!!!
 */
static char * mlv_read_debug_log(const char *mlv_filename)
{
    FILE **chunk_files = NULL;
    uint32_t chunk_count = 0;
    
    chunk_files = mlvfs_load_chunks(mlv_filename, &chunk_count);
    if(!chunk_files || !chunk_count)
    {
        return NULL;
    }
    
    mlv_xref_hdr_t *block_xref = get_index(mlv_filename);
    if (!block_xref)
    {
        mlvfs_close_chunks(chunk_files, chunk_count);
        return NULL;
    }
    mlv_xref_t *xrefs = (mlv_xref_t *)&(((uint8_t*)block_xref)[sizeof(mlv_xref_hdr_t)]);
    
    mlv_hdr_t mlv_hdr;
    mlv_debg_hdr_t debg_hdr;
    uint32_t hdr_size;
    char * result = NULL;

    for(uint32_t block_xref_pos = 0; block_xref_pos < block_xref->entryCount; block_xref_pos++)
    {
        /* get the file and position of the next block */
        uint32_t in_file_num = xrefs[block_xref_pos].fileNumber;
        int64_t position = xrefs[block_xref_pos].frameOffset;
        
        /* select file */
        FILE *in_file = chunk_files[in_file_num];
        
        if(xrefs[block_xref_pos].frameType == MLV_FRAME_UNSPECIFIED)
        {
            file_set_pos(in_file, position, SEEK_SET);
            if(fread(&mlv_hdr, sizeof(mlv_hdr_t), 1, in_file))
            {
                file_set_pos(in_file, position, SEEK_SET);
                if(!memcmp(mlv_hdr.blockType, "DEBG", 4))
                {
                    hdr_size = MIN(sizeof(mlv_debg_hdr_t), mlv_hdr.blockSize);
                    if(fread(&debg_hdr, hdr_size, 1, in_file))
                    {
                        char * temp = NULL;
                        if(result)
                        {
                            size_t current_size = strlen(result);
                            result = realloc(result, current_size + debg_hdr.length + 1);
                            temp = result + current_size;
                        }
                        else
                        {
                            result = malloc(debg_hdr.length + 1);
                            temp = result;
                        }
                        if(result)
                        {
                            if(fread(temp, debg_hdr.length, 1, in_file))
                            {
                                //make sure the string is terminated
                                if(temp[debg_hdr.length - 1] != 0)
                                {
                                    temp[debg_hdr.length] = 0;
                                }
                            }
                        }
                        else
                        {
                            int err = errno;
                            err_printf("malloc error: %s\n", strerror(err));
                        }
                    }
                }
            }
            if(ferror(in_file))
            {
                int err = errno;
                err_printf("fread error: %s\n", strerror(err));
            }
        }
    }

    free(block_xref);
    mlvfs_close_chunks(chunk_files, chunk_count);

    return result;
}

/**
 * Retrieves all the mlv headers associated a particular video frame
 * @param path The path to the MLV file containing the video frame
 * @param index The index of the video frame
 * @param frame_headers [out] All of the MLV blocks associated with the frame
 * @return 1 if successful, 0 otherwise
 */
int mlv_get_frame_headers(const char *mlv_filename, int index, struct frame_headers * frame_headers)
{
    FILE **chunk_files = NULL;
    uint32_t chunk_count = 0;

    chunk_files = mlvfs_load_chunks(mlv_filename, &chunk_count);
    if(!chunk_files || !chunk_count)
    {
        return 0;
    }

    memset(frame_headers, 0, sizeof(struct frame_headers));

    mlv_xref_hdr_t *block_xref = get_index(mlv_filename);
    if (!block_xref)
    {
        mlvfs_close_chunks(chunk_files, chunk_count);
        return 0;
    }

    mlv_xref_t *xrefs = (mlv_xref_t *)&(((uint8_t*)block_xref)[sizeof(mlv_xref_hdr_t)]);

    int found = 0;
    int rawi_found = 0;
    uint32_t vidf_counter = 0;
    mlv_hdr_t mlv_hdr;
    uint32_t hdr_size;

    for(uint32_t block_xref_pos = 0; (block_xref_pos < block_xref->entryCount) && !found; block_xref_pos++)
    {
        /* get the file and position of the next block */
        uint32_t in_file_num = xrefs[block_xref_pos].fileNumber;
        int64_t position = xrefs[block_xref_pos].frameOffset;

        /* select file */
        FILE *in_file = chunk_files[in_file_num];

        switch(xrefs[block_xref_pos].frameType)
        {
            case MLV_FRAME_VIDF:
                //Matches to number in sequence rather than frameNumber in header for consistency with readdir
                if(index == vidf_counter)
                {
                    found = 1;
                    frame_headers->fileNumber = in_file_num;
                    frame_headers->position = position;
                    file_set_pos(in_file, position, SEEK_SET);
                    fread(&mlv_hdr, sizeof(mlv_hdr_t), 1, in_file);
                    file_set_pos(in_file, position, SEEK_SET);
                    hdr_size = MIN(sizeof(mlv_vidf_hdr_t), mlv_hdr.blockSize);
                    fread(&frame_headers->vidf_hdr, hdr_size, 1, in_file);
                }
                else
                {
                    vidf_counter++;
                }
                break;

            case MLV_FRAME_AUDF:
                break;

            case MLV_FRAME_UNSPECIFIED:
            default:
                file_set_pos(in_file, position, SEEK_SET);
                if(fread(&mlv_hdr, sizeof(mlv_hdr_t), 1, in_file))
                {
                    file_set_pos(in_file, position, SEEK_SET);
                    if(!memcmp(mlv_hdr.blockType, "MLVI", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_file_hdr_t), mlv_hdr.blockSize);
                        fread(&frame_headers->file_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "RTCI", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_rtci_hdr_t), mlv_hdr.blockSize);
                        fread(&frame_headers->rtci_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "IDNT", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_idnt_hdr_t), mlv_hdr.blockSize);
                        fread(&frame_headers->idnt_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "RAWI", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_rawi_hdr_t), mlv_hdr.blockSize);
                        if(fread(&frame_headers->rawi_hdr, hdr_size, 1, in_file))
                        {
                            rawi_found = 1;
                        }
                    }
                    else if(!memcmp(mlv_hdr.blockType, "EXPO", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_expo_hdr_t), mlv_hdr.blockSize);
                        fread(&frame_headers->expo_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "LENS", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_lens_hdr_t), mlv_hdr.blockSize);
                        fread(&frame_headers->lens_hdr, hdr_size, 1, in_file);
                    }
                    else if(!memcmp(mlv_hdr.blockType, "WBAL", 4))
                    {
                        hdr_size = MIN(sizeof(mlv_wbal_hdr_t), mlv_hdr.blockSize);
                        fread(&frame_headers->wbal_hdr, hdr_size, 1, in_file);
                    }
                }
        }
        
        if(ferror(in_file))
        {
            int err = errno;
            err_printf("%s: fread error: %s\n", mlv_filename, strerror(err));
        }
    }
    
    if(found && !rawi_found)
    {
        err_printf("%s: Error reading frame headers: no rawi block was found\n", mlv_filename);
    }
    
    if(!found)
    {
        err_printf("%s: Error reading frame headers: vidf block for frame %d was not found\n", mlv_filename, index);
    }
    
    free(block_xref);
    mlvfs_close_chunks(chunk_files, chunk_count);

    return found && rawi_found;
}

/**
 * Retrieves and unpacks image data for a requested section of a video frame
 * @param frame_headers The MLV blocks associated with the frame
 * @param file The file containing the frame data
 * @param output_buffer [out] The buffer to write the result into
 * @param offset The offset into the frame to retrieve
 * @param max_size The amount of frame data to read
 * @return the number of bytes retrieved, or 0 if failure.
 */
size_t get_image_data(struct frame_headers * frame_headers, FILE * file, uint8_t * output_buffer, off_t offset, size_t max_size)
{
    int lzma_compressed = frame_headers->file_hdr.videoClass & MLV_VIDEO_CLASS_FLAG_LZMA;
    int lj92_compressed = frame_headers->file_hdr.videoClass & MLV_VIDEO_CLASS_FLAG_LJ92;
    size_t result = 0;
    int bpp = frame_headers->rawi_hdr.raw_info.bits_per_pixel;
    uint64_t pixel_start_index = MAX(0, offset) / 2; //lets hope offsets are always even for now
    uint64_t pixel_start_address = pixel_start_index * bpp / 16;
    size_t output_size = max_size - (offset < 0 ? (size_t)(-offset) : 0);
    uint64_t pixel_count = output_size / 2;
    uint64_t packed_size = (pixel_count + 2) * bpp / 16;
    if(lzma_compressed || lj92_compressed)
    {
        file_set_pos(file, frame_headers->position + frame_headers->vidf_hdr.frameSpace + sizeof(mlv_vidf_hdr_t), SEEK_SET);
        size_t frame_size = frame_headers->vidf_hdr.blockSize - (frame_headers->vidf_hdr.frameSpace + sizeof(mlv_vidf_hdr_t));
        uint8_t * frame_buffer = malloc(frame_size);
        if (!frame_buffer)
        {
            return 0;
        }
        
        fread(frame_buffer, sizeof(uint8_t), frame_size, file);
        if(ferror(file))
        {
            int err = errno;
            err_printf("fread error: %s\n", strerror(err));
        }
        else
        {
            if(lzma_compressed)
            {
                size_t lzma_out_size = *(uint32_t *)frame_buffer;
                size_t lzma_in_size = frame_size - LZMA_PROPS_SIZE - 4;
                size_t lzma_props_size = LZMA_PROPS_SIZE;
                uint8_t *lzma_out = malloc(lzma_out_size);
                
                int ret = LzmaUncompress(lzma_out, &lzma_out_size,
                                         &frame_buffer[4 + LZMA_PROPS_SIZE], &lzma_in_size,
                                         &frame_buffer[4], lzma_props_size);
                if(ret == SZ_OK)
                {
                    result = dng_get_image_data(frame_headers, (uint16_t*)lzma_out, output_buffer, offset, max_size);
                }
                else
                {
                    err_printf("LZMA Failed!\n");
                }
            }
            else if(lj92_compressed)
            {
                lj92 handle;
                int lj92_width = 0;
                int lj92_height = 0;
                int lj92_bitdepth = 0;
                int video_xRes = frame_headers->rawi_hdr.xRes;
                int video_yRes = frame_headers->rawi_hdr.yRes;
                
                int ret = lj92_open(&handle, (uint8_t *)&frame_buffer[4], (int)frame_size - 4, &lj92_width, &lj92_height, &lj92_bitdepth);
                
                size_t out_size_stored = *(uint32_t *)frame_buffer;
                size_t out_size = lj92_width * lj92_height * sizeof(uint16_t);
                
                if(out_size != out_size_stored)
                {
                    err_printf("LJ92: non-critical internal error occurred: frame size mismatch (%d != %d)\n", (uint32_t)out_size, (uint32_t)out_size_stored);
                }
                
                if(ret == LJ92_ERROR_NONE)
                {
                    /* we need a temporary buffer so we dont overwrite source data */
                    uint16_t *decompressed = malloc(out_size);
                    if (!decompressed)
                    {
                        free(frame_buffer);
                        err_printf("LJ92 malloc failed!\n");
                        return 0;
                    }
                    
                    ret = lj92_decode(handle, decompressed, lj92_width * lj92_height, 0, NULL, 0);
                    
                    if(ret == LJ92_ERROR_NONE)
                    {
                        /* restore 16bpp pixel data and untile if necessary */
                        //uint32_t shift_value = MIN(16,MAX(0, 16 - lj92_bitdepth));
                        uint16_t *dst_buf = (uint16_t *)output_buffer;
                        uint16_t *src_buf = (uint16_t *)decompressed;
                        
                        for(int y = 0; y < video_yRes; y++)
                        {
                            int dst_y = ((2 * y) % video_yRes) + ((2 * y) / video_yRes);
                            
                            uint16_t *src_line = &src_buf[y * video_xRes];
                            uint16_t *dst_line = &dst_buf[dst_y * video_xRes];
                            
                            for(int x = 0; x < video_xRes; x++)
                            {
                                int dst_x = ((2 * x) % video_xRes) + ((2 * x) / video_xRes);
                                dst_line[dst_x] = src_line[x];
                            }
                        }
                        
                        free(decompressed);
                    }
                    else
                    {
                        err_printf("LJ92: Failed (%d)\n", ret);
                    }
                }
                else
                {
                    err_printf("LJ92: Failed (%d)\n", ret);
                }
            }
        }
        free(frame_buffer);
    }
    else
    {
        uint16_t * packed_bits = calloc((size_t)(packed_size * 2), 1);
        if(packed_bits)
        {
            
            file_set_pos(file, frame_headers->position + frame_headers->vidf_hdr.frameSpace + sizeof(mlv_vidf_hdr_t) + pixel_start_address * 2, SEEK_SET);
            fread(packed_bits, sizeof(uint16_t), (size_t)packed_size, file);
            if(ferror(file))
            {
                int err = errno;
                err_printf("fread error: %s\n", strerror(err));
            }
            else
            {
                result = dng_get_image_data(frame_headers, packed_bits, output_buffer, offset, max_size);
            }
            free(packed_bits);
        }
    }
    return result;
}

/**
 * Generates a customizable virtual name for the MLV file (for the virtual directory)
 * Make sure you free() the result!!!
 * @param path The virtual path
 * @param mlv_basename [out] The MLV basename
 * @return 1 if successful, 0 otherwise
 */
static int get_mlv_basename(const char *path, char ** mlv_basename)
{
    if(!(string_ends_with(path, ".MLV") || string_ends_with(path, ".mlv"))) return 0;
    char *temp = copy_string(path);
    const char *start = find_last_separator(temp) ? find_last_separator(temp) + 1 : temp;
    char *dot = strchr(start, '.');
    if(dot == NULL) { free(temp); return 0; }
    *dot = '\0';
    struct frame_headers frame_headers;
    if(mlvfs.name_scheme == 1 && mlv_get_frame_headers(path, 0, &frame_headers))
    {
        *mlv_basename =  malloc(sizeof(char) * (strlen(start) + 1024));
        sprintf(*mlv_basename, "%s_1_%d-%02d-%02d_%04d_C%04d", start, 1900 + frame_headers.rtci_hdr.tm_year, frame_headers.rtci_hdr.tm_mon + 1, frame_headers.rtci_hdr.tm_mday, 1, 0);
    }
    else
    {
        *mlv_basename = copy_string(start);
    }
    free(temp);
    return 1;
}

static void check_mld_exists(char * path)
{
    char *temp = copy_string(path);
    char *mld_ext = strstr(temp, ".MLD");

    if(mld_ext != NULL)
    {
        *(mld_ext + 4) = 0x0;
        struct stat mld_stat;
        if(stat(temp, &mld_stat))
        {
#ifdef _WIN32
            mkdir(temp);
#else
            mkdir(temp, 0777);
#endif
        }
    }
    free(temp);
}

static void deflicker(struct frame_headers * frame_headers, int target, uint16_t * data, size_t size)
{
    uint16_t black = frame_headers->rawi_hdr.raw_info.black_level;
    uint16_t white = (1 << frame_headers->rawi_hdr.raw_info.bits_per_pixel) + 1;
    
    struct histogram * hist = hist_create(white);
    hist_add(hist, data + 1, (uint32_t)((size -  1) / 2), 1);
    uint16_t median = hist_median(hist);
    double correction = log2((double) (target - black) / (median - black));
    frame_headers->rawi_hdr.raw_info.exposure_bias[0] = correction * 10000;
    frame_headers->rawi_hdr.raw_info.exposure_bias[1] = 10000;
}

static int process_frame(struct image_buffer * image_buffer)
{
    char * mlv_filename = NULL;
    const char * path = image_buffer->dng_filename;
    
    if(string_ends_with(path, ".dng") && get_mlv_filename(path, &mlv_filename))
    {
        int frame_number = get_mlv_frame_number(path);
        struct frame_headers frame_headers;
        if(mlv_get_frame_headers(mlv_filename, frame_number, &frame_headers))
        {
            FILE **chunk_files = NULL;
            uint32_t chunk_count = 0;
            
            chunk_files = mlvfs_load_chunks(mlv_filename, &chunk_count);
            if(!chunk_files || !chunk_count)
            {
                free(mlv_filename);
                return 0;
            }
            
            image_buffer->size = dng_get_image_size(&frame_headers);
            image_buffer->data = (uint16_t*)malloc(image_buffer->size);
            image_buffer->header_size = dng_get_header_size();
            image_buffer->header = (uint8_t*)malloc(image_buffer->header_size);
            
            char * mlv_basename = copy_string(image_buffer->dng_filename);
            if(mlv_basename != NULL)
            {
                char * dir = find_last_separator(mlv_basename);
                if(dir != NULL) *dir = 0;
            }
            
            get_image_data(&frame_headers, chunk_files[frame_headers.fileNumber], (uint8_t*) image_buffer->data, 0, image_buffer->size);
            if(mlvfs.deflicker) deflicker(&frame_headers, mlvfs.deflicker, image_buffer->data, image_buffer->size);
            dng_get_header_data(&frame_headers, image_buffer->header, 0, image_buffer->header_size, mlvfs.fps, mlv_basename);
            
            if(mlvfs.fix_bad_pixels)
            {
                fix_bad_pixels(&frame_headers, image_buffer->data, mlvfs.fix_bad_pixels == 2);
            }
            
            if(mlvfs.fix_pattern_noise)
            {
                fix_pattern_noise((int16_t*)image_buffer->data, frame_headers.rawi_hdr.xRes, frame_headers.rawi_hdr.yRes, frame_headers.rawi_hdr.raw_info.white_level, 0);
            }
            
            int is_dual_iso = 0;
            if(mlvfs.dual_iso == 1)
            {
                is_dual_iso = hdr_convert_data(&frame_headers, image_buffer->data, 0, image_buffer->size);
            }
            else if(mlvfs.dual_iso == 2)
            {
                is_dual_iso = cr2hdr20_convert_data(&frame_headers, image_buffer->data, mlvfs.hdr_interpolation_method, !mlvfs.hdr_no_fullres, !mlvfs.hdr_no_alias_map, mlvfs.chroma_smooth);
            }
            
            if(is_dual_iso)
            {
                //redo the dng header b/c white and black levels will be different
                dng_get_header_data(&frame_headers, image_buffer->header, 0, image_buffer->size, mlvfs.fps, mlv_basename);
            }
            else
            {
                fix_focus_pixels(&frame_headers, image_buffer->data, 0);
            }
            
            if(mlvfs.chroma_smooth && mlvfs.dual_iso != 2)
            {
                chroma_smooth(&frame_headers, image_buffer->data, mlvfs.chroma_smooth);
            }
            
            if(mlvfs.fix_stripes)
            {
                struct stripes_correction * correction = stripes_get_correction(mlv_filename);
                if(correction == NULL)
                {
                    correction = stripes_new_correction(mlv_filename);
                    if(correction)
                    {
                        stripes_compute_correction(&frame_headers, correction, image_buffer->data, 0, image_buffer->size / 2);
                    }
                    else
                    {
                        int err = errno;
                        err_printf("malloc error: %s\n", strerror(err));
                    }
                }
                stripes_apply_correction(&frame_headers, correction, image_buffer->data, 0, image_buffer->size / 2);
            }
            mlvfs_close_chunks(chunk_files, chunk_count);
            free(mlv_basename);
        }
        free(mlv_filename);
    }
    return 1;
}

int create_preview(struct image_buffer * image_buffer)
{
    char * mlv_filename = NULL;
    const char * path = image_buffer->dng_filename;
    
    if(string_ends_with(path, ".gif") && get_mlv_filename(path, &mlv_filename))
    {
        struct frame_headers frame_headers;
        if(mlv_get_frame_headers(mlv_filename, 0, &frame_headers))
        {
            image_buffer->size = gif_get_size(&frame_headers);
            image_buffer->data = (uint16_t*)malloc(image_buffer->size);
            image_buffer->header_size = 0;
            image_buffer->header = NULL;
            gif_get_data(mlv_filename, (uint8_t*)image_buffer->data, 0, image_buffer->size);
        }
        free(mlv_filename);
    }
    return 1;
}


static int is_mlv_file(char *filename)
{
    if (string_ends_with(filename, ".MLV") || string_ends_with(filename, ".mlv"))
    {
        /* just treat as if it were existing. will fail later */
        // struct STAT64 mlv_stat;
        // if (STAT64(filename, &mlv_stat) == 0)
        {
            return 1;
        }
    }
    return 0;
}

/**
* check if the given path is within a MLV file or a MLV itself
* Make sure you free() the result
* @return 1 if the real path is a MLV or inside a MLV, 0 otherwise
*/
static int mlvfs_resolve_path(const char *path, char **mlv_file, char **path_in_mlv)
{
    int ret = 0;
    int done = 0;
    const char *current_token = path;
    char *current_path = malloc(strlen(path) + 16);
    current_path[0] = 0;

    while (!done)
    {
        /* skip leading slashes */
        while (find_first_separator(current_token) == current_token)
        {
            current_token++;
        }

        /* check if the current path is already a valid MLV */
        char *mlv_file_check = path_append(mlvfs.mlv_path, path_slashfix(current_path));

        if (is_mlv_file(mlv_file_check))
        {
            /* ok, return MLV path and virtual file path (or empty) */
            *mlv_file = path_append(mlvfs.mlv_path, path_slashfix(current_path));
            *path_in_mlv = copy_string(current_token);
            done = 1;
            ret = 1;
        }
        else if (*current_token == 0)
        {
            /* no more tokens, its not a virtual file */
            done = 1;
        }
        else
        {
            /* no slash in the front */
            if (strlen(current_path) != 0)
            {
                strcat(current_path, DIR_SEP_STR);
            }

            /* is there another slash? */
            char *token_end = find_first_separator((const char*)current_token);
            if (token_end)
            {
                /* yes, add token until slash */
                uint32_t length = (uint32_t)((uintmax_t)token_end - (uintmax_t)current_token);
                strncat(current_path, current_token, length);
                current_token = token_end + 1;
            }
            else
            {
                /* no, add remaining token */
                strcat(current_path, current_token);
                current_token += strlen(current_token);
            }
        }
        free(mlv_file_check);
    }

    free(current_path);

    return ret;
}

/**
* try to find the real file path from given virtual path
* Make sure you free() the result
* @return NULL if this is a pure virtual file or a char* with the name of the file on disk
*/
static char *mlvfs_resolve_virtual(const char *path)
{
    char *mlv_filename = NULL;
    char *path_in_mlv = NULL;
    char *resolved_filename = NULL;

    /* is this within a virtual directory? */
    if (mlvfs_resolve_path(path, &mlv_filename, &path_in_mlv))
    {
        int is_in_mlv_root = (find_first_separator(path_in_mlv) == NULL);

        if (is_in_mlv_root && (string_ends_with(path_in_mlv, ".dng") || string_ends_with(path_in_mlv, ".wav") || string_ends_with(path_in_mlv, ".gif") || string_ends_with(path_in_mlv, ".log")))
        {
            /* a DNG etc in the MLV root -> virtual */
            resolved_filename = NULL;
        }
        else if (strlen(path_in_mlv) == 0)
        {
            /* it is the MLV itself */
            resolved_filename = copy_string(mlv_filename);
        }
        else
        {
            char *mld_name = copy_string(mlv_filename);
            char *dot = strrchr(mld_name, '.');

            if (dot)
            {
                strcpy(dot, ".MLD");
            }

            resolved_filename = path_append(mld_name, path_slashfix(path_in_mlv));

            free(mld_name);
        }
        free(mlv_filename);
        free(path_in_mlv);
    }
    else
    {
        /* this file is not within a virtual directory, so just get it from the existing one */
        char *tmp_path = path_slashfix(copy_string(path));
        resolved_filename = path_append((const char*)mlvfs.mlv_path, (const char*)tmp_path);
        free(tmp_path);
    }

    return resolved_filename;
}

static int mlvfs_getattr(const char *path, struct FUSE_STAT *stbuf)
{
    memset(stbuf, 0, sizeof(struct FUSE_STAT));

    int result = -ENOENT;
    char *mlv_filename = NULL;
    char *path_in_mlv = NULL;
    char *resolved_filename = NULL;

    /* try to find the real file on disk */
    resolved_filename = mlvfs_resolve_virtual(path);

    if (resolved_filename)
    {
        /* now try to get the file stat */
        struct STAT64 file_stat;
        int stat_code = STAT64(resolved_filename, &file_stat);

        /* if that is a MLV file, fake it to be a directory */
        if (is_mlv_file(resolved_filename))
        {
            stbuf->st_mode = S_IFDIR | 0777;
            stbuf->st_nlink = 3;

#ifdef WIN32
            stbuf->st_atim = timeToTimestruct(file_stat.st_atime);
            stbuf->st_ctim = timeToTimestruct(file_stat.st_ctime);
            stbuf->st_mtim = timeToTimestruct(file_stat.st_mtime);
#elif __DARWIN_UNIX03
            memcpy(&stbuf->st_atimespec, &file_stat.st_atimespec, sizeof(struct timespec));
            memcpy(&stbuf->st_birthtimespec, &file_stat.st_birthtimespec, sizeof(struct timespec));
            memcpy(&stbuf->st_ctimespec, &file_stat.st_ctimespec, sizeof(struct timespec));
            memcpy(&stbuf->st_mtimespec, &file_stat.st_mtimespec, sizeof(struct timespec));
#else
            memcpy(&stbuf->st_atim, &file_stat.st_atim, sizeof(struct timespec));
            memcpy(&stbuf->st_ctim, &file_stat.st_ctim, sizeof(struct timespec));
            memcpy(&stbuf->st_mtim, &file_stat.st_mtim, sizeof(struct timespec));
#endif
        }
        else if (stat_code == 0)
        {
            stbuf->st_uid = file_stat.st_uid;
            stbuf->st_gid = file_stat.st_gid;
            stbuf->st_mode = file_stat.st_mode;
            stbuf->st_size = file_stat.st_size;
            stbuf->st_nlink = file_stat.st_nlink;
            stbuf->st_rdev = file_stat.st_rdev;
            stbuf->st_dev = file_stat.st_dev;
            stbuf->st_ino = file_stat.st_ino;
#if defined(__DARWIN_UNIX03)
            memcpy(&stbuf->st_atimespec, &file_stat.st_atimespec, sizeof(struct timespec));
            memcpy(&stbuf->st_birthtimespec, &file_stat.st_birthtimespec, sizeof(struct timespec));
            memcpy(&stbuf->st_ctimespec, &file_stat.st_ctimespec, sizeof(struct timespec));
            memcpy(&stbuf->st_mtimespec, &file_stat.st_mtimespec, sizeof(struct timespec));
#elif defined(_WIN32)
            stbuf->st_atim = timeToTimestruct(file_stat.st_atime);
            stbuf->st_ctim = timeToTimestruct(file_stat.st_ctime);
            stbuf->st_mtim = timeToTimestruct(file_stat.st_mtime);
#else
            stbuf->st_atim = file_stat.st_atim;
            stbuf->st_ctim = file_stat.st_ctim;
            stbuf->st_mtim = file_stat.st_mtim;
#endif
        }
        free(resolved_filename);

        return (stat_code == 0) ? 0 : -ENOENT;
    }

    /* so this must be a virtual file, fetch MLV name and path */
    if (mlvfs_resolve_path(path, &mlv_filename, &path_in_mlv))
    {
        if (string_ends_with(path_in_mlv, ".dng") || string_ends_with(path_in_mlv, ".wav") || string_ends_with(path_in_mlv, ".gif") || string_ends_with(path_in_mlv, ".log"))
        {
            /* if it's a file in root, all accesses to DNG, WAV, GIF and LOG are redirected */
            struct FUSE_STAT * dng_st = NULL;

            if (string_ends_with(path_in_mlv, ".dng") && (dng_st = lookup_dng_attr(mlv_filename)) != NULL)
            {
                memcpy(stbuf, dng_st, sizeof(struct FUSE_STAT));
                result = 0;
            }
            else
            {
                int frame_number = string_ends_with(path_in_mlv, ".dng") ? get_mlv_frame_number(path_in_mlv) : 0;

#ifdef ALLOW_WRITEABLE_DNGS
                stbuf->st_mode = S_IFREG | 0666;
#else
                stbuf->st_mode = S_IFREG | 0444;
#endif
                stbuf->st_nlink = 1;

                struct frame_headers frame_headers;
                if (mlv_get_frame_headers(mlv_filename, frame_number, &frame_headers))
                {
                    struct tm tm_str;
                    tm_str.tm_sec = (int)(frame_headers.rtci_hdr.tm_sec + (frame_headers.vidf_hdr.timestamp - frame_headers.rtci_hdr.timestamp) / 1000000);
                    tm_str.tm_min = frame_headers.rtci_hdr.tm_min;
                    tm_str.tm_hour = frame_headers.rtci_hdr.tm_hour;
                    tm_str.tm_mday = frame_headers.rtci_hdr.tm_mday;
                    tm_str.tm_mon = frame_headers.rtci_hdr.tm_mon;
                    tm_str.tm_year = frame_headers.rtci_hdr.tm_year;
                    tm_str.tm_isdst = frame_headers.rtci_hdr.tm_isdst;

                    struct timespec timespec_str;
                    timespec_str.tv_sec = mktime(&tm_str);
                    timespec_str.tv_nsec = ((frame_headers.vidf_hdr.timestamp - frame_headers.rtci_hdr.timestamp) % 1000000) * 1000;

                    // OS-specific timestamps
#if __DARWIN_UNIX03
                    memcpy(&stbuf->st_atimespec, &timespec_str, sizeof(struct timespec));
                    memcpy(&stbuf->st_birthtimespec, &timespec_str, sizeof(struct timespec));
                    memcpy(&stbuf->st_ctimespec, &timespec_str, sizeof(struct timespec));
                    memcpy(&stbuf->st_mtimespec, &timespec_str, sizeof(struct timespec));
#else
                    memcpy(&stbuf->st_atim, &timespec_str, sizeof(struct timespec));
                    memcpy(&stbuf->st_ctim, &timespec_str, sizeof(struct timespec));
                    memcpy(&stbuf->st_mtim, &timespec_str, sizeof(struct timespec));
#endif

                    if (string_ends_with(path_in_mlv, ".dng"))
                    {
                        stbuf->st_size = dng_get_size(&frame_headers);
                        register_dng_attr(mlv_filename, stbuf);
                    }
                    else if (string_ends_with(path_in_mlv, ".gif"))
                    {
                        stbuf->st_size = gif_get_size(&frame_headers);
                    }
                    else if (string_ends_with(path_in_mlv, ".log"))
                    {
                        char * log = mlv_read_debug_log(mlv_filename);
                        if (log)
                        {
                            stbuf->st_size = strlen(log);
                            free(log);
                        }
                    }
                    else
                    {
                        stbuf->st_size = wav_get_size(mlv_filename);
                    }
                    result = 0; // DNG frame found
                }
            }
        }
        free(mlv_filename);
        free(path_in_mlv);
    }

    return result;
}

static int mlvfs_open(const char *path, struct fuse_file_info *fi)
{
    int result = 0;

    /* reset the cached image buffer, if any */
    fi->fh = 0;

    /* try to find the real file on disk */
    char *resolved_filename = mlvfs_resolve_virtual(path);

    if (resolved_filename)
    {
        int fd = open(resolved_filename, O_RDONLY | O_BINARY);
        free(resolved_filename);

        if (fd < 0)
        {
            return -errno;
        }

        /* always close file after read/write operations. else deleting etc will fail on windows */
        close(fd);

        return 0;
    }
    
    #ifndef ALLOW_WRITEABLE_DNGS
    if ((fi->flags & O_ACCMODE) != O_RDONLY) /* Only reading allowed. */
        result = -EACCES;
    #endif
    
    return result;
}

static int mlvfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, FUSE_OFF_T offset, struct fuse_file_info *fi)
{
    char *real_path = NULL;
    char *mlv_filename = NULL;
    char *path_in_mlv = NULL;
    int result = -ENOENT;
    int is_mld_dir = 0;

    if (string_ends_with(path, ".MLD"))
    {
        return -ENOENT;
    }

    /* first check if that directory can be resolved */
    if (mlvfs_resolve_path(path, &mlv_filename, &path_in_mlv))
    {
        /* it refers to a subdir (existing or not) */
        if (strlen(path_in_mlv) > 0)
        {
            real_path = mlvfs_resolve_virtual(path);
        }
        else
        {
            /* it refers to the MLV itself */
            filler(buf, ".", NULL, 0);
            filler(buf, "..", NULL, 0);
            is_mld_dir = 1;

            char * mlv_basename = NULL;
            get_mlv_basename(mlv_filename, &mlv_basename);

            char *filename = malloc(sizeof(char) * (strlen(mlv_basename) + 1024));
            if (filename)
            {
                if (has_audio(mlv_filename))
                {
                    sprintf(filename, "%s.wav", mlv_basename);
                    filler(buf, filename, NULL, 0);
                }
                sprintf(filename, "%s.log", mlv_basename);
                filler(buf, filename, NULL, 0);
                int frame_count = mlv_get_frame_count(mlv_filename);
                for (int i = 0; i < frame_count; i++)
                {
                    sprintf(filename, "%s_%06d.dng", mlv_basename, i);
                    filler(buf, filename, NULL, 0);
                }
                sprintf(filename, "_PREVIEW.gif");
                filler(buf, filename, NULL, 0);
                result = 0;

                /* now pass over the MLD dir to the "real" directory listing code */
                real_path = copy_string(mlv_filename);
                char *dot = strrchr(real_path, '.');

                if (dot)
                {
                    strcpy(dot, ".MLD");
                }

                free(filename);
            }
            else
            {
                int err = errno;
                err_printf("malloc error: %s\n", strerror(err));
                result = -ENOENT;
            }
        }
    }
    else
    {
        /* its not within a MLV */
        char *tmp_path = path_slashfix(copy_string(path));
        real_path = path_append((const char*)mlvfs.mlv_path, (const char*)tmp_path);
        free(tmp_path);
    }

    if (real_path)
    {
        DIR * dir = opendir(real_path);

        if (dir != NULL)
        {
            if (!is_mld_dir)
            {
                filler(buf, ".", NULL, 0);
                filler(buf, "..", NULL, 0);
            }
            struct dirent * child;

            while ((child = readdir(dir)) != NULL)
            {
                /* ignore MLD directories and ./.. as we already put them */
                if (string_ends_with(child->d_name, ".MLD") || string_ends_with(child->d_name, ".IDX") || !strcmp(child->d_name, "..") || !strcmp(child->d_name, "."))
                {
                    continue;
                }

                char * real_file_path = path_append(real_path, child->d_name);
                char * mlv_basename = NULL;

                if (mlvfs.name_scheme && get_mlv_basename(real_file_path, &mlv_basename))
                {
                    filler(buf, mlv_basename, NULL, 0);
                    char * virtual_path = path_append(path, mlv_basename);
                    register_mlv_name(real_file_path, virtual_path);
                    free(virtual_path);
                    free(mlv_basename);
                }
                else if (string_ends_with(child->d_name, ".MLV") || string_ends_with(child->d_name, ".mlv") || child->d_type == DT_DIR || is_mld_dir)
                {
                    filler(buf, child->d_name, NULL, 0);
                }
                else if (child->d_type == DT_UNKNOWN) // If d_type is not supported on this filesystem
                {
                    struct stat file_stat;
                    if ((stat(real_file_path, &file_stat) == 0) && S_ISDIR(file_stat.st_mode))
                    {
                        filler(buf, child->d_name, NULL, 0);
                    }
                }
                free(real_file_path);
            }
            closedir(dir);
            result = 0;
        }
        free(real_path);
    }

    return result;
}

static int mlvfs_read(const char *path, char *buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info *fi)
{
    /* if we already had a read on that handle and it was a .dng, it's info will be cached */
    if (fi->fh == 0)
    {
        /* files are always opened/closed before/after any file operation */
        char *resolved_filename = mlvfs_resolve_virtual(path);
        int fd = -1;

        if (resolved_filename)
        {
            fd = open(resolved_filename, O_RDONLY | O_BINARY);
            free(resolved_filename);

            if (fd < 0)
            {
                return -errno;
            }

            int res = (int)pread(fd, buf, size, offset);
            if (res < 0)
            {
                res = -errno;
            }

            /* always close file after read/write operations. else deleting etc will fail on windows */
            close(fd);

            return res;
        }
    }

    char *mlv_filename = NULL;
    char *path_in_mlv = NULL;

    /* if there is no handle, it must be a virtual file, or it is an already opened .dng */
    if (fi->fh || mlvfs_resolve_path(path, &mlv_filename, &path_in_mlv))
    {
        if (fi->fh || string_ends_with(path_in_mlv, ".dng"))
        {
            size_t header_size = dng_get_header_size();
            size_t remaining = 0;
            off_t image_offset = 0;
            int was_created = 0;

            struct image_buffer * image_buffer = (struct image_buffer *)fi->fh;
            
            /* was the image buffer already cached? */
            if (!image_buffer)
            {
                image_buffer = get_or_create_image_buffer(path, &process_frame, &was_created);
            }

            if (!image_buffer)
            {
                err_printf("DNG image_buffer is NULL\n");
                return 0;
            }
            if (!image_buffer->header)
            {
                err_printf("DNG image_buffer->header is NULL\n");
                return 0;
            }
            if (!image_buffer->data)
            {
                err_printf("DNG image_buffer->data is NULL\n");
                return 0;
            }

            /* cache the expensive locking/lookup for a potential next read */
            fi->fh = (uint64_t)image_buffer;

            /* sanitize parameters to prevent errors by accesses beyond end */
            long file_size = image_buffer->header_size + image_buffer->size;
            long read_offset = MAX(0, MIN(offset, file_size));
            long read_size = MAX(0, MIN(size, file_size - read_offset));

            if (read_offset + read_size > file_size)
            {
                read_size = (size_t)(file_size - read_offset);
            }

            if (read_offset < header_size && image_buffer->header_size > 0)
            {
                remaining = MIN(read_size, header_size - read_offset);
                memcpy(buf, image_buffer->header + read_offset, remaining);
            }
            else
            {
                image_offset = read_offset - header_size;
            }

            if (remaining < read_size && image_buffer->size > 0)
            {
                uint8_t* image_output_buf = (uint8_t*)buf + remaining;
                memcpy(image_output_buf, ((uint8_t*)image_buffer->data) + image_offset, MIN(read_size - remaining, image_buffer->size - image_offset));
            }

            free(mlv_filename);
            return (int)read_size;
        }
        else if (string_ends_with(path_in_mlv, ".wav"))
        {
            int result = (int)wav_get_data(mlv_filename, (uint8_t*)buf, offset, size);
            free(mlv_filename);
            return result;
        }
        else if (string_ends_with(path_in_mlv, ".gif"))
        {
            int was_created;
            struct image_buffer * image_buffer = get_or_create_image_buffer(path, &create_preview, &was_created);
            if (!image_buffer)
            {
                err_printf("GIF image_buffer is NULL\n");
                return 0;
            }
            if (!image_buffer->data)
            {
                err_printf("GIF image_buffer->data is NULL\n");
                return 0;
            }

            /* ensure that reads with offset beyond end will not cause negative memcpy sizes */
            long read_offset = MAX(0, MIN(offset, image_buffer->size));
            long read_size = MAX(0, MIN(size, image_buffer->size - read_offset));

            memcpy(buf, ((uint8_t*)image_buffer->data) + read_offset, read_size);
            free(mlv_filename);
            return (int)read_size;
        }
        else if (string_ends_with(path_in_mlv, ".log"))
        {
            char * log = mlv_read_debug_log(mlv_filename);
            size_t read_bytes = 0;

            if (log)
            {
                if (offset < strlen(log))
                {
                    read_bytes = MIN(size, strlen(log) - offset);
                    memcpy(buf, log + offset, read_bytes);
                }
                free(log);
            }
            free(mlv_filename);
            return (int)read_bytes;
        }
    }
    
    return -ENOENT;
}

static int mlvfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    /* try to find the real file on disk */
    char *resolved_filename = mlvfs_resolve_virtual(path);

    /* it's a virtual file */
    if (!resolved_filename)
    {
        return -EPERM;
    }

    check_mld_exists(resolved_filename);

#if _WIN32
    /* on windows only these modes are allowed */
    mode &= (_S_IREAD | _S_IWRITE);
#endif

    int fd = creat(resolved_filename, mode);
    free(resolved_filename);

    if (fd < 0)
    {
        return -errno;
    }

    /* always close file after read/write operations. else deleting etc will fail on windows */
    close(fd);

    return 0;
}

static int mlvfs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    /* we always close files on every read/write, no need to fsync */
    return 0;
}

static int mlvfs_mkdir(const char *path, mode_t mode)
{
    int result = -ENOENT;
    char * real_path = mlvfs_resolve_virtual(path);
    if(real_path)
    {
        check_mld_exists(real_path);
#ifdef _WIN32
        mkdir(real_path);
#else
        mkdir(real_path, mode);
#endif
        free(real_path);
        result = 0;
    }
    return result;
}

static int mlvfs_release(const char *path, struct fuse_file_info *fi)
{
    /* reset the cached image buffer pointer, if any */
    fi->fh = 0;

    if (string_ends_with(path, ".dng") || string_ends_with(path, ".gif"))
    {
        release_image_buffer_by_path(path);
    }
    return 0;
}

static int mlvfs_rename(const char *from, const char *to)
{
    int result = -ENOENT;
    char * real_from = mlvfs_resolve_virtual(from);
    char * real_to = mlvfs_resolve_virtual(to);

    if(real_from && real_to)
    {
        dbg_printf("real_path '%s' -> '%s'\n", real_from, real_to);
        rename(real_from, real_to);
        result = 0;
    }
    if (real_from)
    {
        free(real_from);
    }
    if (real_to)
    {
        free(real_to);
    }
    return result;
}

static int mlvfs_rmdir(const char *path)
{
    int result = -ENOENT;
    char * real_path = mlvfs_resolve_virtual(path);
    if (real_path)
    {
        dbg_printf("real_path '%s'\n", real_path);
        rmdir(real_path);
        free(real_path);
        result = 0;
    }
    return result;
}

static int mlvfs_unlink(const char *path)
{
    int result = -EPERM;
    char * real_path = mlvfs_resolve_virtual(path);
    if (real_path)
    {
        dbg_printf("real_path '%s'\n", real_path);
        unlink(real_path);
        free(real_path);
        result = 0;
    }
    return result;
}

static int mlvfs_truncate(const char *path, FUSE_OFF_T offset)
{
    int result = -EPERM;
    char * real_path = mlvfs_resolve_virtual(path);
    if(real_path)
    {
        dbg_printf("real_path '%s'\n", real_path);
        truncate(real_path, offset);
        free(real_path);
        result = 0;
    }
    return result;
}

static int mlvfs_write(const char *path, const char *buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info *fi)
{
    /* files are always opened/closed before/after any file operation */
    char *resolved_filename = mlvfs_resolve_virtual(path);
    int fd = -1;

    if (resolved_filename)
    {
        fd = open(resolved_filename, O_RDWR | O_BINARY);
        free(resolved_filename);
    }

    if (fd < 0)
    {
        return -errno;
    }

    int res = (int)pwrite(fd, buf, size, offset);
    if (res < 0)
    {
        res = -errno;
    }

    /* always close file after read/write operations. else deleting etc will fail on windows */
    close(fd);

    return res;
}

static int mlvfs_statfs(const char *path, struct statvfs *stat)
{
    stat->f_bsize = 512;
    stat->f_blocks = (1 * 1024 * 1024 * 1024) / stat->f_bsize;
    stat->f_bfree = stat->f_blocks;
    stat->f_bavail = stat->f_blocks;

    return 0;
}

static int mlvfs_wrap_getattr(const char *path, struct FUSE_STAT *stbuf)
{
    dbg_printf("'%s' 0x%08X\n", path, (uint32_t)stbuf);
    TRY_WRAP(return mlvfs_getattr(path, stbuf); )
}
static int mlvfs_wrap_open(const char *path, struct fuse_file_info *fi)
{
    dbg_printf("'%s' 0x%08X\n", path, (uint32_t)fi);
    TRY_WRAP(return mlvfs_open(path, fi); )
}
static int mlvfs_wrap_readdir(const char *path, void *buf, fuse_fill_dir_t filler, FUSE_OFF_T offset, struct fuse_file_info *fi)
{
    dbg_printf("'%s' 0x%08X 0x%08X 0x%08X 0x%08X\n", path, (uint32_t)buf, (uint32_t)filler, (uint32_t)offset, (uint32_t)fi);
    TRY_WRAP(return mlvfs_readdir(path, buf, filler, offset, fi); )
}
static int mlvfs_wrap_read(const char *path, char *buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info *fi)
{
    dbg_printf("'%s' 0x%08X 0x%08X 0x%08X 0x%08X\n", path, (uint32_t)buf, (uint32_t)size, (uint32_t)offset, (uint32_t)fi);
    TRY_WRAP(return mlvfs_read(path, buf, size, offset, fi); )
}
static int mlvfs_wrap_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    dbg_printf("'%s' 0x%08X 0x%08X\n", path, (uint32_t)mode, (uint32_t)fi);
    TRY_WRAP(return mlvfs_create(path, mode, fi); )
}
static int mlvfs_wrap_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    dbg_printf("'%s' 0x%08X 0x%08X\n", path, (uint32_t)isdatasync, (uint32_t)fi);
    TRY_WRAP(return mlvfs_fsync(path, isdatasync, fi); )
}
static int mlvfs_wrap_mkdir(const char *path, mode_t mode)
{
    dbg_printf("'%s' 0x%08X\n", path, (uint32_t)mode);
    TRY_WRAP(return mlvfs_mkdir(path, mode); )
}
static int mlvfs_wrap_release(const char *path, struct fuse_file_info *fi)
{
    dbg_printf("'%s' 0x%08X\n", path, (uint32_t)fi);
    TRY_WRAP(return mlvfs_release(path, fi); )
}
static int mlvfs_wrap_rename(const char *from, const char *to)
{
    dbg_printf("'%s' '%s'\n", from, to);
    TRY_WRAP(return mlvfs_rename(from, to); )
}
static int mlvfs_wrap_rmdir(const char *path)
{
    dbg_printf("'%s'\n", path);
    TRY_WRAP(return mlvfs_rmdir(path); )
}
static int mlvfs_wrap_truncate(const char *path, FUSE_OFF_T offset)
{
    dbg_printf("'%s' 0x%08X\n", path, (uint32_t)offset);
    TRY_WRAP(return mlvfs_truncate(path, offset); )
}
static int mlvfs_wrap_write(const char *path, const char *buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info *fi)
{
    dbg_printf("'%s' 0x%08X 0x%08X 0x%08X 0x%08X\n", path, (uint32_t)buf, (uint32_t)size, (uint32_t)offset, (uint32_t)fi);
    TRY_WRAP(return mlvfs_write(path, buf, size, offset, fi); )
}
static int mlvfs_wrap_statfs(const char *path, struct statvfs *stat)
{
    dbg_printf("'%s' 0x%08X\n", path, (uint32_t)stat);
    TRY_WRAP(return mlvfs_statfs(path, stat); )
}
static int mlvfs_wrap_unlink(const char *path)
{
    dbg_printf("'%s'\n", path);
    TRY_WRAP(return mlvfs_unlink(path); )
}

static struct fuse_operations mlvfs_filesystem_operations =
{
    .getattr     = mlvfs_wrap_getattr,
    .open        = mlvfs_wrap_open,
    .read        = mlvfs_wrap_read,
    .readdir     = mlvfs_wrap_readdir,
    .create      = mlvfs_wrap_create,
    .fsync       = mlvfs_wrap_fsync,
    .mkdir       = mlvfs_wrap_mkdir,
    .release     = mlvfs_wrap_release,
    .rename      = mlvfs_wrap_rename,
    .rmdir       = mlvfs_wrap_rmdir,
    .truncate    = mlvfs_wrap_truncate,
    .write       = mlvfs_wrap_write,
    .statfs      = mlvfs_wrap_statfs,
    .unlink      = mlvfs_wrap_unlink
};

static const struct fuse_opt mlvfs_opts[] =
{
    { "--mlv_dir=%s",       offsetof(struct mlvfs, mlv_path),                   0 },
    { "--mlv-dir=%s",       offsetof(struct mlvfs, mlv_path),                   0 },
    { "--port=%s",          offsetof(struct mlvfs, port),                       0 },
    { "--resolve-naming",   offsetof(struct mlvfs, name_scheme),                1 },
    { "--cs2x2",            offsetof(struct mlvfs, chroma_smooth),              2 },
    { "--cs3x3",            offsetof(struct mlvfs, chroma_smooth),              3 },
    { "--cs5x5",            offsetof(struct mlvfs, chroma_smooth),              5 },
    { "--bad-pix",          offsetof(struct mlvfs, fix_bad_pixels),             1 },
    { "--really-bad-pix",   offsetof(struct mlvfs, fix_bad_pixels),             2 },
    { "--stripes",          offsetof(struct mlvfs, fix_stripes),                1 },
    { "--dual-iso-preview", offsetof(struct mlvfs, dual_iso),                   1 },
    { "--dual-iso",         offsetof(struct mlvfs, dual_iso),                   2 },
    { "--amaze-edge",       offsetof(struct mlvfs, hdr_interpolation_method),   0 },
    { "--mean23",           offsetof(struct mlvfs, hdr_interpolation_method),   1 },
    { "--no-alias-map",     offsetof(struct mlvfs, hdr_no_alias_map),           1 },
    { "--alias-map",        offsetof(struct mlvfs, hdr_no_alias_map),           0 },
    { "--fps=%f",           offsetof(struct mlvfs, fps),                        0 },
    { "--deflicker=%d",     offsetof(struct mlvfs, deflicker),                  0 },
    { "--fix-pattern-noise",offsetof(struct mlvfs, fix_pattern_noise),          1 },
    { "--version",          offsetof(struct mlvfs, version),                    1 },
    FUSE_OPT_END
};

static void display_help()
{
    printf("\n");

    /* display FUSE options */
    char * help_opts[] = {"mlvfs", "-h"};
#ifndef _WIN32
    fuse_main(2, help_opts, NULL, NULL);
#endif

    /* display MLVFS options */
    /* todo: print a description for each option */
    printf("\nMLVFS options:\n");
    int num_opts = sizeof(mlvfs_opts) / sizeof(mlvfs_opts[0]) - 1;
    for (int i = 1; i < num_opts; i++)
    {
        printf("    %s\n", mlvfs_opts[i].templ);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    mlvfs.mlv_path = NULL;
    mlvfs.chroma_smooth = 0;

    int res = 1;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    
    if (fuse_opt_parse(&args, &mlvfs, mlvfs_opts, NULL) == -1)
    {
        exit(1);
    }

    if (mlvfs.version)
    {
        printf("Version: %s\n", VERSION);
        printf("Date:    %s\n", BUILD_DATE);
    }
    else if (mlvfs.mlv_path != NULL)
    {
        //init luts
        get_raw2evf(0);
        get_raw2ev(0);
        get_ev2raw();
        
        // shell and wildcard expansion, taking just the first result
        char *expanded_path = NULL;

#ifdef _WIN32
        char expanded[MAX_PATH];
        ExpandEnvironmentStrings(mlvfs.mlv_path, expanded, sizeof(expanded));
        expanded_path = _strdup(expanded);
#else
        wordexp_t p;
        wordexp(mlvfs.mlv_path, &p, 0);
        expanded_path = strdup(p.we_wordv[0]);
        wordfree(&p);
#endif

        // check if the directory actually exists
        struct stat file_stat;
        if ((stat(mlvfs.mlv_path, &file_stat) == 0) && S_ISDIR(file_stat.st_mode))
        {
            res = 0;
        }
        else if ((stat(expanded_path, &file_stat) == 0) && S_ISDIR(file_stat.st_mode))
        {
            // assume that p.we_wordc > 0
            free(mlvfs.mlv_path); // needs to be freed due to below pointer re-assignment
            mlvfs.mlv_path = expanded_path;
            res = 0;
        }
        else
        {
            err_printf("MLVFS: mlv path is not a directory\n");
        }

        if(!res)
        {
            webgui_start(&mlvfs);
            umask(0);
            res = fuse_main(args.argc, args.argv, &mlvfs_filesystem_operations, NULL);
        }

        free(expanded_path);
    }
    else
    {
        err_printf("MLVFS: no mlv path specified\n");
        display_help();
    }

    fuse_opt_free_args(&args);
    webgui_stop();
    stripes_free_corrections();
    free_all_image_buffers();
    close_all_chunks();
    free_mlv_name_mappings();
    free_dng_attr_mappings();
    free_focus_pixel_maps();
    return res;
}
