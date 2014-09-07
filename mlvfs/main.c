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

#define FUSE_USE_VERSION 26

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <wordexp.h>
#include <fuse.h>
#include <sys/param.h>
#include "raw.h"
#include "mlv.h"
#include "dng.h"
#include "index.h"
#include "wav.h"

struct mlvfs
{
    char * mlv_path;
};

static struct mlvfs mlvfs;

static int string_ends_with(const char *source, const char *ending)
{
    if(source == NULL || ending == NULL) return 0;
    if(strlen(source) <= 0) return 0;
    if(strlen(source) < strlen(ending)) return 0;
    return !strcmp(source + strlen(source) - strlen(ending), ending);
}

//returns 1 if apparently within an MLV file, 0 otherwise
static int get_mlv_filename(const char *path, char * mlv_filename)
{
    char temp[1024];
    char *split;
    strncpy(temp, path, 1024);
    if((split = strrchr(temp + 1, '/')) != NULL)
    {
        *split = 0x00;
        sprintf(mlv_filename, "%s%s", mlvfs.mlv_path, temp);
        return (string_ends_with(mlv_filename, ".MLV") || string_ends_with(mlv_filename, ".mlv"));
    }
    return 0;
}

static int get_mlv_frame_number(const char *path)
{
    char temp[1024];
    strncpy(temp, path, 1024);
    char * str_number = strrchr(temp, '/');
    if(str_number)
    {
        str_number+=3;
        char * dot = strrchr(str_number, '.');
        *dot = 0x0;
        return atoi(str_number);
    }
    return 0;
}

//returns 1 if successful, 0 otherwise
static int mlv_get_frame_headers(const char *path, int index, struct frame_headers * frame_headers)
{
    char mlv_filename[1024];
    if (!get_mlv_filename(path, mlv_filename)) return 0;

    FILE **chunk_files = NULL;
    uint32_t chunk_count = 0;

    chunk_files = load_chunks(mlv_filename, &chunk_count);
    if(!chunk_files || !chunk_count)
    {
        return 0;
    }

    memset(frame_headers, 0, sizeof(struct frame_headers));

    mlv_xref_hdr_t *block_xref = get_index(mlv_filename);
    mlv_xref_t *xrefs = (mlv_xref_t *)&(((uint8_t*)block_xref)[sizeof(mlv_xref_hdr_t)]);

    int found = 0;
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
                    fseeko(in_file, position, SEEK_SET);
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
                fseeko(in_file, position, SEEK_SET);
                fread(&mlv_hdr, sizeof(mlv_hdr_t), 1, in_file);
                fseeko(in_file, position, SEEK_SET);
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
                    fread(&frame_headers->rawi_hdr, hdr_size, 1, in_file);
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

    free(block_xref);
    close_chunks(chunk_files, chunk_count);

    return found;
}

static int mlvfs_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    if (string_ends_with(path, ".DNG") || string_ends_with(path, ".WAV"))
    {
        char mlv_filename[1024];
        if(get_mlv_filename(path, mlv_filename))
        {
            stbuf->st_mode = S_IFREG | 0444;
            stbuf->st_nlink = 1;
                
            struct frame_headers frame_headers;
            int frame_number = string_ends_with(path, ".DNG") ? get_mlv_frame_number(path) : 0;
            if(mlv_get_frame_headers(path, frame_number, &frame_headers))
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

                if(string_ends_with(path, ".DNG"))
                {
                    stbuf->st_size = dng_get_size(&frame_headers);
                }
                else
                {
                    stbuf->st_size = wav_get_size(mlv_filename);
                }

                return 0; // DNG frame found
            }
        }
    }
    else
    {
        char real_path[1024];
        sprintf(real_path, "%s%s", mlvfs.mlv_path, path);
        struct stat mlv_stat;
        if(stat(real_path, &mlv_stat) == 0)
        {
            stbuf->st_mode = S_IFDIR | 0555;
            stbuf->st_nlink = 3;
            
            // OS-specific timestamps
            #if __DARWIN_UNIX03
            memcpy(&stbuf->st_atimespec, &mlv_stat.st_atimespec, sizeof(struct timespec));
            memcpy(&stbuf->st_birthtimespec, &mlv_stat.st_birthtimespec, sizeof(struct timespec));
            memcpy(&stbuf->st_ctimespec, &mlv_stat.st_ctimespec, sizeof(struct timespec));
            memcpy(&stbuf->st_mtimespec, &mlv_stat.st_mtimespec, sizeof(struct timespec));
            #else
            memcpy(&stbuf->st_atim, &mlv_stat.st_atim, sizeof(struct timespec));
            memcpy(&stbuf->st_ctim, &mlv_stat.st_ctim, sizeof(struct timespec));
            memcpy(&stbuf->st_mtim, &mlv_stat.st_mtim, sizeof(struct timespec));
            #endif
            
            if (string_ends_with(path, ".MLV") || string_ends_with(path, ".mlv"))
            {
                stbuf->st_size = mlv_get_frame_count(real_path);
            }
            else
            {
                stbuf->st_size = mlv_stat.st_size;
            }
            
            return 0; // MLV or directory found
        }
    }

    return -ENOENT;
}

static int mlvfs_open(const char *path, struct fuse_file_info *fi)
{
    if (!(string_ends_with(path, ".DNG") || string_ends_with(path, ".WAV")))
        return -ENOENT;
    
    if ((fi->flags & O_ACCMODE) != O_RDONLY) /* Only reading allowed. */
        return -EACCES;
    
    return 0;
}

static int mlvfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    char real_path[1024];
    sprintf(real_path, "%s%s", mlvfs.mlv_path, path);
    if(string_ends_with(path, ".MLV") || string_ends_with(path, ".mlv"))
    {
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);
        if(has_audio(real_path))
        {
            filler(buf, "_AUDIO.WAV", NULL, 0);
        }
        int frame_count = mlv_get_frame_count(real_path);
        for (int i = 0; i < frame_count; i++)
        {
            sprintf(real_path, "%08d.DNG", i);
            filler(buf, real_path, NULL, 0);
        }
        return 0;
    }
    else
    {
        DIR * dir = opendir(real_path);
        if (dir != NULL)
        {
            filler(buf, ".", NULL, 0);
            filler(buf, "..", NULL, 0);
            
            struct dirent * child;
            
            while ((child = readdir(dir)) != NULL)
            {
                if(string_ends_with(child->d_name, ".MLV") || string_ends_with(child->d_name, ".mlv") || child->d_type == DT_DIR)
                {
                    filler(buf, child->d_name, NULL, 0);
                }
                else if (child->d_type == DT_UNKNOWN) // If d_type is not supported on this filesystem
                {
                    struct stat file_stat;
                    char real_file_path[1024];
                    sprintf(real_file_path, "%s/%s", real_path, child->d_name);
                    if ((stat(real_file_path, &file_stat) == 0) && S_ISDIR(file_stat.st_mode))
                    {
                        filler(buf, child->d_name, NULL, 0);
                    }
                }
            }
            closedir(dir);
            return 0;
        }
    }
    return -ENOENT;
}

static int mlvfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    char mlv_filename[1024];
    if(string_ends_with(path, ".DNG") && get_mlv_filename(path, mlv_filename))
    {
        int frame_number = get_mlv_frame_number(path);
        struct frame_headers frame_headers;
        if(mlv_get_frame_headers(path, frame_number, &frame_headers))
        {
            FILE **chunk_files = NULL;
            uint32_t chunk_count = 0;

            chunk_files = load_chunks(mlv_filename, &chunk_count);
            if(!chunk_files || !chunk_count)
            {
                return -1;
            }

            size_t dng_size = dng_get_size(&frame_headers);
            if(offset + size > dng_size)
            {
                size = (size_t)(dng_size - offset);
            }

            size_t header_size = dng_get_header_size(&frame_headers);
            if(offset >= header_size)
            {
                dng_get_image_data(&frame_headers, chunk_files[frame_headers.fileNumber], (uint8_t*)buf, offset - header_size, size);
            }
            else
            {
                size_t remaining = MIN(size, header_size - offset);
                dng_get_header_data(&frame_headers, (uint8_t*)buf, offset, remaining);
                if(remaining < size)
                {
                    dng_get_image_data(&frame_headers, chunk_files[frame_headers.fileNumber], (uint8_t*)buf + remaining, 0, size - remaining);
                }
            }

            close_chunks(chunk_files, chunk_count);
        }
        else
        {
            size = 0;
        }
        return (int)size;
    }
    else if(string_ends_with(path, ".WAV") && get_mlv_filename(path, mlv_filename))
    {
        return (int)wav_get_data(mlv_filename, (uint8_t*)buf, offset, size);
    }
    
    return -ENOENT;
}

static struct fuse_operations mlvfs_filesystem_operations =
{
    .getattr = mlvfs_getattr,
    .open    = mlvfs_open,
    .read    = mlvfs_read,
    .readdir = mlvfs_readdir,
};

static const struct fuse_opt mlvfs_opts[] =
{
    { "--mlv_dir=%s", offsetof(struct mlvfs, mlv_path), 0 },
    FUSE_OPT_END
};

int main(int argc, char **argv)
{
    mlvfs.mlv_path = NULL;

    int res = 1;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    
    if (fuse_opt_parse(&args, &mlvfs, mlvfs_opts, NULL) == -1)
    {
        exit(1);
    }

    if (mlvfs.mlv_path != NULL)
    {
        // shell and wildcard expansion, taking just the first result
        wordexp_t p;
        wordexp(mlvfs.mlv_path, &p, 0);

        // assume that p.we_wordc > 0
        free(mlvfs.mlv_path);
        mlvfs.mlv_path = p.we_wordv[0];

        // check if the directory actually exists
        struct stat file_stat;
        if ((stat(mlvfs.mlv_path, &file_stat) == 0) && S_ISDIR(file_stat.st_mode))
        {
            umask(0);
            res = fuse_main(args.argc, args.argv, &mlvfs_filesystem_operations, NULL);
        }
        else
        {
            fprintf(stderr, "MLVFS: mount path is not a directory\n");
        }
    }
    else
    {
        fprintf(stderr, "MLVFS: no mount path specified\n");
    }

    fuse_opt_free_args(&args);
    return res;
}
