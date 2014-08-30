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
#include <stdlib.h>
#include <stddef.h>
#include <fuse.h>
#include <sys/param.h>
#include "raw.h"
#include "mlv.h"
#include "dng.h"

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

static int get_mlv_filename(const char *path, char * mlv_filename)
{
    char temp[1024];
    char *split;
    strncpy(temp, path, 1024);
    if((split = strrchr(temp, '/')) != NULL)
    {
        *split = 0x00;
        sprintf(mlv_filename, "%s%s", mlvfs.mlv_path, temp);
        return 1;
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

static int mlv_get_frame_headers(FILE * mlv_file, int index, struct frame_headers * frame_headers)
{
    mlv_hdr_t mlv_hdr;
    uint32_t hdr_size;
    unsigned int position = ftell(mlv_file);
    int found = 0;
    memset(frame_headers, 0, sizeof(struct frame_headers));
    //TODO: use an index file rather than searching the whole file for a certain frame
    while(fread(&mlv_hdr, sizeof(mlv_hdr_t), 1, mlv_file))
    {
        fseek(mlv_file, position, SEEK_SET);
        if(!memcmp(mlv_hdr.blockType, "VIDF", 4))
        {
            hdr_size = MIN(sizeof(mlv_vidf_hdr_t), mlv_hdr.blockSize);
            
            if(fread(&frame_headers->vidf_hdr, hdr_size, 1, mlv_file))
            {
                if(frame_headers->vidf_hdr.frameNumber == index)
                {
                    found = 1;
                    frame_headers->position = position;
                    fseek(mlv_file, position , SEEK_SET);
                    break;
                }
            }
            else
            {
                fprintf(stderr, "File ends in the middle of a block\n");
                break;
            }
        }
        else if(!memcmp(mlv_hdr.blockType, "MLVI", 4))
        {
            hdr_size = MIN(sizeof(mlv_file_hdr_t), mlv_hdr.blockSize);
            
            if(!fread(&frame_headers->file_hdr, hdr_size, 1, mlv_file))
            {
                fprintf(stderr, "File ends in the middle of a block\n");
                break;
            }
        }
        else if(!memcmp(mlv_hdr.blockType, "RTCI", 4))
        {
            hdr_size = MIN(sizeof(mlv_rtci_hdr_t), mlv_hdr.blockSize);
            
            if(!fread(&frame_headers->rtci_hdr, hdr_size, 1, mlv_file))
            {
                fprintf(stderr, "File ends in the middle of a block\n");
                break;
            }
        }
        else if(!memcmp(mlv_hdr.blockType, "RAWI", 4))
        {
            hdr_size = MIN(sizeof(mlv_rawi_hdr_t), mlv_hdr.blockSize);
            
            if(!fread(&frame_headers->rawi_hdr, hdr_size, 1, mlv_file))
            {
                fprintf(stderr, "File ends in the middle of a block\n");
                break;
            }
        }
        else if(!memcmp(mlv_hdr.blockType, "EXPO", 4))
        {
            hdr_size = MIN(sizeof(mlv_expo_hdr_t), mlv_hdr.blockSize);
            
            if(!fread(&frame_headers->expo_hdr, hdr_size, 1, mlv_file))
            {
                fprintf(stderr, "File ends in the middle of a block\n");
                break;
            }
        }
        else if(!memcmp(mlv_hdr.blockType, "LENS", 4))
        {
            hdr_size = MIN(sizeof(mlv_lens_hdr_t), mlv_hdr.blockSize);
            
            if(!fread(&frame_headers->lens_hdr, hdr_size, 1, mlv_file))
            {
                fprintf(stderr, "File ends in the middle of a block\n");
                break;
            }
        }
        else if(!memcmp(mlv_hdr.blockType, "WBAL", 4))
        {
            hdr_size = MIN(sizeof(mlv_wbal_hdr_t), mlv_hdr.blockSize);
            
            if(!fread(&frame_headers->wbal_hdr, hdr_size, 1, mlv_file))
            {
                fprintf(stderr, "File ends in the middle of a block\n");
                break;
            }
        }
        
        fseek(mlv_file, position + mlv_hdr.blockSize, SEEK_SET);
        position = ftell(mlv_file);
    }
    return found;
}

static size_t mlv_get_dng_size(const char *path)
{
    size_t result = 0;
    struct frame_headers frame_headers;
    FILE * mlv_file = fopen(path, "rb");
    if(mlv_file)
    {
        int frame_number = get_mlv_frame_number(path);
        if(mlv_get_frame_headers(mlv_file, frame_number, &frame_headers))
        {
            result = dng_get_size(&frame_headers);
        }
        fclose(mlv_file);
    }
    else
    {
        fprintf(stderr, "Could not open file: '%s'\n", path);
    }
    return result;
}

//TODO: load chunks too
static int mlv_get_frame_count(const char *path)
{
    int result = 0;
    mlv_hdr_t mlv_hdr;
    unsigned int position = 0;
    FILE * mlv_file = fopen(path, "rb");
    if(mlv_file)
    {
        position = ftell(mlv_file);
        while(fread(&mlv_hdr, sizeof(mlv_hdr_t), 1, mlv_file))
        {
            fseek(mlv_file, position, SEEK_SET);
            if(!memcmp(mlv_hdr.blockType, "MLVI", 4))
            {
                mlv_file_hdr_t file_hdr;
                uint32_t hdr_size = sizeof(mlv_file_hdr_t) < mlv_hdr.blockSize ? sizeof(mlv_file_hdr_t) : mlv_hdr.blockSize;
                
                //if there's a frame count provided just return that, otherwise we need to count VIDF blocks
                if(fread(&file_hdr, hdr_size, 1, mlv_file))
                {
                    if(file_hdr.videoFrameCount)
                    {
                        result = file_hdr.videoFrameCount;
                        break;
                    }
                }
                else
                {
                    fprintf(stderr, "File ends in the middle of a block\n");
                    break;
                }
            }
            
            if(!memcmp(mlv_hdr.blockType, "VIDF", 4))
            {
                result++;
            }
            
            fseek(mlv_file, position + mlv_hdr.blockSize, SEEK_SET);
            position = ftell(mlv_file);
        }
        fclose(mlv_file);
    }
    else
    {
        fprintf(stderr, "Could not open file: '%s'\n", path);
    }
    return result;
}

static int mlvfs_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    
    if (string_ends_with(path, ".DNG"))
    {
        char mlv_filename[1024];
        if(get_mlv_filename(path, mlv_filename))
        {
            struct stat mlv_stat;
            stbuf->st_mode = S_IFREG | 0444;
            stbuf->st_nlink = 1;
            stbuf->st_size = mlv_get_dng_size(mlv_filename);
            if(stat(mlv_filename, &mlv_stat) == 0)
            {
                memcpy(&stbuf->st_atimespec, &mlv_stat.st_atimespec, sizeof(struct timespec));
                memcpy(&stbuf->st_birthtimespec, &mlv_stat.st_birthtimespec, sizeof(struct timespec));
                memcpy(&stbuf->st_ctimespec, &mlv_stat.st_ctimespec, sizeof(struct timespec));
                memcpy(&stbuf->st_mtimespec, &mlv_stat.st_mtimespec, sizeof(struct timespec));
            }
        }
        else
        {
            return -ENOENT;
        }
    }
    else
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 3;
    }
    return 0;
}

static int mlvfs_open(const char *path, struct fuse_file_info *fi)
{
    if (!string_ends_with(path, ".DNG"))
        return -ENOENT;
    
    if ((fi->flags & O_ACCMODE) != O_RDONLY) /* Only reading allowed. */
        return -EACCES;
    
    return 0;
}

static int mlvfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    if (!strcmp(path, "/"))
    {
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);
        
        DIR * mlv_dir = opendir(mlvfs.mlv_path);
        if (mlv_dir == NULL)
            return -1;
        
        struct dirent * child;
        
        while ((child = readdir(mlv_dir)) != NULL)
        {
            if(string_ends_with(child->d_name, ".MLV") || string_ends_with(child->d_name, ".mlv"))
            {
                filler(buf, child->d_name, NULL, 0);
            }
        }
        closedir(mlv_dir);
        return 0;
    }
    else if(string_ends_with(path, ".MLV") || string_ends_with(path, ".mlv"))
    {
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);
        char temp[1024];
        sprintf(temp, "%s%s", mlvfs.mlv_path, path);
        int frame_count = mlv_get_frame_count(temp);
        for (int i = 0; i < frame_count; i++)
        {
            sprintf(temp, "%08d.DNG", i);
            filler(buf, temp, NULL, 0);
        }
        return 0;
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
        FILE * mlv_file = fopen(mlv_filename, "rb");
        if(mlv_file)
        {
            if(mlv_get_frame_headers(mlv_file, frame_number, &frame_headers))
            {
                size_t header_size = dng_get_header_size(&frame_headers);
                if(offset < header_size)
                {
                    dng_get_header_data(&frame_headers, (uint8_t*)buf, offset, size);
                }
                if(offset + size > header_size)
                {
                    dng_get_image_data(&frame_headers, mlv_file, (uint8_t*)buf, offset - header_size, size);
                }
            }
            fclose(mlv_file);
        }
        else
        {
            fprintf(stderr, "Could not open file: '%s'\n", path);
        }
        return size;
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
    int res = 0;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    
    if (fuse_opt_parse(&args, &mlvfs, mlvfs_opts, NULL) == -1)
    {
        exit(1);
    }
    
    umask(0);
    res = fuse_main(args.argc, args.argv, &mlvfs_filesystem_operations, NULL);
    
    fuse_opt_free_args(&args);
    return res;
}