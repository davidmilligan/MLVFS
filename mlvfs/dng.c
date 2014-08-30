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

#include <stdio.h>
#include <sys/param.h>
#include "raw.h"
#include "mlv.h"
#include "dng.h"


//TODO: implement this function
size_t dng_get_header_data(struct frame_headers * frame_headers, char * buffer, off_t offset, size_t max_size)
{
    return 0;
}

//TODO: implement this function
size_t dng_get_header_size(struct frame_headers * frame_headers)
{
    return 0;
}

//TODO: implement this function
size_t dng_get_image_data(struct frame_headers * frame_headers, FILE * file, char * buffer, off_t offset, size_t max_size)
{
    fseek(file, frame_headers->position + frame_headers->vidf_hdr.frameSpace + sizeof(mlv_vidf_hdr_t) + (size_t)MIN(0, offset), SEEK_SET);
    return fread(buffer, max_size, 1, file);
}

size_t dng_get_image_size(struct frame_headers * frame_headers)
{
    return frame_headers->rawi_hdr.raw_info.width * frame_headers->rawi_hdr.raw_info.height * 2; //16 bit
}

size_t dng_get_size(struct frame_headers * frame_headers)
{
    return dng_get_header_size(frame_headers) + dng_get_image_size(frame_headers);
}
