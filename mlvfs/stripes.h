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

#ifndef mlvfs_stripes_h
#define mlvfs_stripes_h

#include <stdio.h>
#include "raw.h"
#include "mlv.h"
#include "dng.h"

struct correction
{
    struct correction * next;
    char * mlv_filename;
    int correction_needed;
    uint16_t white;
    int coeffficients[8];
};

struct correction * stripes_get_correction(const char * mlv_filename);
struct correction * stripes_new_correction(const char * mlv_filename);
void stripes_free_corrections();

int stripes_correction_check_needed(struct frame_headers * frame_headers);
void stripes_compute_correction(struct frame_headers * frame_headers, struct correction * correction, uint16_t * image_data, off_t offset, size_t size);
void stripes_apply_correction(struct frame_headers * frame_headers, struct correction * correction, uint16_t * image_data, off_t offset, size_t size);

#endif
