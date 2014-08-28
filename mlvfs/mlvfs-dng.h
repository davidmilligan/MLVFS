//
//  mlvfs-dng.h
//  mlvfs
//
//  Created by David Milligan on 8/27/14.
//  Copyright (c) 2014 Magic Lantern. All rights reserved.
//

#ifndef mlvfs_mlvfs_dng_h
#define mlvfs_mlvfs_dng_h

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define COERCE(x,lo,hi) MAX(MIN((x),(hi)),(lo))

#define DNG_THUMBNAIL_WIDTH     128
#define DNG_THUMBNAIL_HEIGHT    84
#define DNG_THUMBNAIL_SIZE      (DNG_THUMBNAIL_WIDTH * DNG_THUMBNAIL_HEIGHT * 3)

void reverse_bytes_order(char* buf, int count);
char* dng_create_header(struct raw_info * raw_info, int * dng_header_buf_size);
char* dng_create_thumbnail(struct raw_info * raw_info);
int dng_get_size(struct raw_info * raw_info);

#endif
