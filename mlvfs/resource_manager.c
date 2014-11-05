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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "index.h"
#include "mlvfs.h"
#include "resource_manager.h"

CREATE_MUTEX(image_buffer_mutex)

static struct image_buffer * image_buffers = NULL;

static struct image_buffer * get_image_buffer(const char * dng_filename)
{
    for(struct image_buffer * current = image_buffers; current != NULL; current = current->next)
    {
        if(!strcmp(current->dng_filename, dng_filename)) return current;
    }
    return NULL;
}

static struct image_buffer * new_image_buffer(const char * dng_filename)
{
    //TODO: limit the total amount of buffer memory in case programs don't call fclose in a timely manner
    //though this doesn't seem to be an issue with any programs I've used
    struct image_buffer * new_buffer = malloc(sizeof(struct image_buffer));
    if(new_buffer == NULL) return NULL;
    
    memset(new_buffer, 0, sizeof(struct image_buffer));
    
    if(image_buffers == NULL)
    {
        image_buffers = new_buffer;
    }
    else
    {
        struct image_buffer * current = image_buffers;
        while(current->next != NULL)
        {
            current = current->next;
        }
        current->next = new_buffer;
    }
    new_buffer->dng_filename = malloc((sizeof(char) * (strlen(dng_filename) + 2)));
    strcpy(new_buffer->dng_filename, dng_filename);
    INIT_LOCK(new_buffer->mutex);
    return new_buffer;
}

struct image_buffer * get_or_create_image_buffer(const char * path, int(*new_buffer_cbr)(struct image_buffer *))
{
    struct image_buffer * image_buffer = NULL;
    
    RELOCK(image_buffer_mutex)
    {
        image_buffer = get_image_buffer(path);
        if(!image_buffer)
        {
            image_buffer = new_image_buffer(path);
        }
    }
    UNLOCK(image_buffer_mutex)
    
    if(!image_buffer) return NULL;
    
    RELOCK(image_buffer->mutex)
    {
        if(!image_buffer->data)
        {
            new_buffer_cbr(image_buffer);
        }
        image_buffer->in_use = 1;
    }
    UNLOCK(image_buffer->mutex)
    
    return image_buffer;
}

void free_image_buffer(struct image_buffer * image_buffer)
{
    if(!image_buffer) return;
    int in_use = 0;
    RELOCK(image_buffer->mutex)
    {
        in_use = image_buffer->in_use;
        if(in_use)
        {
            image_buffer->needs_destroy = 1;
        }
    }
    UNLOCK(image_buffer->mutex)
    if(in_use) return;
    
    if(image_buffer == image_buffers)
    {
        image_buffers = image_buffer->next;
    }
    else
    {
        struct image_buffer * current = image_buffers;
        while(current->next != NULL)
        {
            if(current->next == image_buffer) break;
            current = current->next;
        }
        current->next = image_buffer->next;
    }
    
    DESTROY_LOCK(image_buffer->mutex);
    free(image_buffer->dng_filename);
    free(image_buffer->data);
    free(image_buffer);
}

void free_image_buffer_by_path(const char * path)
{
    RELOCK(image_buffer_mutex)
    {
        struct image_buffer * image_buffer = get_image_buffer(path);
        if(image_buffer)
        {
            free_image_buffer(image_buffer);
        }
    }
    UNLOCK(image_buffer_mutex)
}

void free_all_image_buffers()
{
    struct image_buffer * next = NULL;
    struct image_buffer * current = image_buffers;
    while(current != NULL)
    {
        next = current->next;
        free(current->dng_filename);
        free(current->data);
        free(current->header);
        free(current);
        current = next;
    }
}

void image_buffer_read_end(struct image_buffer * image_buffer)
{
    int needs_destroy = 0;
    RELOCK(image_buffer->mutex)
    {
        image_buffer->in_use = 0;
        needs_destroy = image_buffer->needs_destroy;
    }
    UNLOCK(image_buffer->mutex)
    
    RELOCK(image_buffer_mutex)
    {
        if(needs_destroy)
        {
            free_image_buffer(image_buffer);
        }
    }
    UNLOCK(image_buffer_mutex)
}

static struct mlv_chunks * loaded_chunks = NULL;

static struct mlv_chunks * get_chunks(const char * path)
{
    for(struct mlv_chunks * current = loaded_chunks; current != NULL; current = current->next)
    {
        if(current->thread_id == CURRENT_THREAD && !strcmp(current->path, path)) return current;
    }
    return NULL;
}

static struct mlv_chunks * new_chunks(const char * path, FILE** files, uint32_t chunk_count)
{
    struct mlv_chunks * new_buffer = (struct mlv_chunks *)malloc(sizeof(struct mlv_chunks));
    if(new_buffer == NULL) return NULL;
    
    if(loaded_chunks == NULL)
    {
        loaded_chunks = new_buffer;
    }
    else
    {
        struct mlv_chunks * current = loaded_chunks;
        while(current->next != NULL)
        {
            current = current->next;
        }
        current->next = new_buffer;
    }
    new_buffer->path = (char*)malloc((sizeof(char) * (strlen(path) + 2)));
    strcpy(new_buffer->path, path);
    new_buffer->next = NULL;
    new_buffer->chunks = files;
    new_buffer->chunk_count = chunk_count;
    new_buffer->thread_id = CURRENT_THREAD;
    return new_buffer;
}

void close_all_chunks()
{
    struct mlv_chunks * next = NULL;
    struct mlv_chunks * current = loaded_chunks;
    while(current != NULL)
    {
        next = current->next;
        free(current->path);
        close_chunks(current->chunks, current->chunk_count);
        free(current);
        current = next;
    }
}

FILE** mlvfs_load_chunks(const char * path, uint32_t * chunk_count)
{
    FILE **chunk_files = NULL;
    *chunk_count = 0;
    
    LOCK(chunk_load_mutex)
    {
        struct mlv_chunks * mlv_chunks = get_chunks(path);
        if(!mlv_chunks)
        {
            chunk_files = load_chunks(path, chunk_count);
            new_chunks(path, chunk_files, *chunk_count);
        }
        else
        {
            chunk_files = mlv_chunks->chunks;
            *chunk_count = mlv_chunks->chunk_count;
        }
    }
    UNLOCK(chunk_load_mutex)
    return chunk_files;
}

CREATE_MUTEX(mlv_name_mapping_mutex)

static struct mlv_name_mapping * mlv_name_mappings = NULL;

static char * lookup_mlv_name_internal(const char * virtual_path)
{
    for(struct mlv_name_mapping * current = mlv_name_mappings; current != NULL; current = current->next)
    {
        if(!strcmp(current->virtual_path, virtual_path)) return current->real_path;
    }
    return NULL;
}

char * lookup_mlv_name(const char * virtual_path)
{
    char * result = NULL;
    RELOCK(mlv_name_mapping_mutex)
    {
        result = lookup_mlv_name_internal(virtual_path);
    }
    UNLOCK(mlv_name_mapping_mutex)
    return result;
}

void register_mlv_name(const char * real_path, const char * virtual_path)
{
    RELOCK(mlv_name_mapping_mutex)
    {
        if(!lookup_mlv_name_internal(virtual_path))
        {
            struct mlv_name_mapping * new_buffer = (struct mlv_name_mapping *)malloc(sizeof(struct mlv_name_mapping));
            if(new_buffer)
            {
                new_buffer->real_path = (char*)malloc((sizeof(char) * (strlen(real_path) + 2)));
                strcpy(new_buffer->real_path, real_path);
                new_buffer->virtual_path = (char*)malloc((sizeof(char) * (strlen(virtual_path) + 2)));
                strcpy(new_buffer->virtual_path, virtual_path);
                new_buffer->next = mlv_name_mappings;
                mlv_name_mappings = new_buffer;
            }
        }
    }
    UNLOCK(mlv_name_mapping_mutex)
}

void free_mlv_name_mappings()
{
    RELOCK(mlv_name_mapping_mutex)
    {
        struct mlv_name_mapping * next = NULL;
        struct mlv_name_mapping * current = mlv_name_mappings;
        while(current != NULL)
        {
            next = current->next;
            free(current->real_path);
            free(current->virtual_path);
            free(current);
            current = next;
        }
    }
    UNLOCK(mlv_name_mapping_mutex)
}