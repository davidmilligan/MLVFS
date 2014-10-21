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

#ifndef mlvfs_resource_manager_h
#define mlvfs_resource_manager_h

#include <stdio.h>

struct image_buffer
{
    struct image_buffer * next;
    char * dng_filename;
    size_t header_size;
    size_t size;
    uint8_t * header;
    uint16_t * data;
    LOCK_T mutex;
    int in_use;
    int needs_destroy;
};

struct image_buffer * get_or_create_image_buffer(const char * path, int(*new_buffer_cbr)(struct image_buffer *));
void free_image_buffer(struct image_buffer * image_buffer);
void free_image_buffer_by_path(const char * path);
void free_all_image_buffers();
void image_buffer_read_end(struct image_buffer * image_buffer);


struct mlv_chunks
{
    struct mlv_chunks * next;
    char *path;
    THREAD_T thread_id;
    uint32_t chunk_count;
    FILE** chunks;
};

FILE** mlvfs_load_chunks(const char * path, uint32_t * chunk_count);
void close_all_chunks();


struct mlv_name_mapping
{
    struct mlv_name_mapping * next;
    char *virtual_path;
    char *real_path;
};

char * lookup_mlv_name(const char * virtual_path);
void register_mlv_name(const char * real_path, const char * virtual_path);
void free_mlv_name_mappings();

#endif
