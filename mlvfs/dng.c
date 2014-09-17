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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "raw.h"
#include "mlv.h"
#include "dng.h"

#include "dng_tag_codes.h"
#include "dng_tag_types.h"
#include "dng_tag_values.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define IFD0_COUNT 34
#define EXIF_IFD_COUNT 8
#define MLVFS_SOFTWARE_NAME "MLVFS"
#define PACK(a) (((uint16_t)a[1] << 16) | ((uint16_t)a[0]))
#define PACK2(a,b) (((uint16_t)b << 16) | ((uint16_t)a))
#define STRING_ENTRY(a,b,c) (uint32_t)(strlen(a) + 1), add_string(a, b, c)
#define RATIONAL_ENTRY(a,b,c,d) (d/2), add_array(a, b, c, d)
#define RATIONAL_ENTRY2(a,b,c,d) 1, add_rational(a, b, c, d)
#define ARRAY_ENTRY(a,b,c,d) d, add_array(a, b, c, d)
#define LINEARIZATION_TABLE(a,b,c) a, add_linearization_table(a, b, c)
#define HEADER_SIZE 65536

static uint16_t tiff_header[] = { byteOrderII, magicTIFF, 8, 0};

static int32_t daylight_wbal[] = {473635,1000000,1000000,1000000,624000,1000000};

struct directory_entry {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value;
};

//CDNG tag codes
enum
{
	tcTimeCodes				= 51043,
    tcFrameRate             = 51044,
    tcTStop                 = 51058,
    tcReelName              = 51081,
    tcCameraLabel           = 51105,
};

//MLV WB modes
enum
{
    WB_AUTO         = 0,
    WB_SUNNY        = 1,
    WB_SHADE        = 8,
    WB_CLOUDY       = 2,
    WB_TUNGSTEN     = 3,
    WB_FLUORESCENT  = 4,
    WB_FLASH        = 5,
    WB_CUSTOM       = 6,
    WB_KELVIN       = 9
};


static uint32_t add_linearization_table(uint32_t max, uint8_t * buffer, uint32_t * data_offset)
{
    uint32_t result = *data_offset;
    for(uint16_t curr = 0; curr < max; curr++)
    {
        *(uint16_t*)(buffer + *data_offset) = curr;
        *data_offset += sizeof(uint16_t);
    }
    return result;
}

static uint32_t add_array(int32_t * array, uint8_t * buffer, uint32_t * data_offset, size_t length)
{
    uint32_t result = *data_offset;
    memcpy(buffer + result, array, length * sizeof(int32_t));
    *data_offset += length * sizeof(int32_t);
    return result;
}

static uint32_t add_string(char * str, uint8_t * buffer, uint32_t * data_offset)
{
    uint32_t result = 0;
    size_t length = strlen(str) + 1;
    if(length <= 4)
    {
        //we can fit in 4 bytes, so just pack the string into result
        memcpy(&result, str, length);
    }
    else
    {
        result = *data_offset;
        memcpy(buffer + result, str, length);
        *data_offset += length;
        //align to 2 bytes
        if(*data_offset % 2) *data_offset += 1;
    }
    return result;
}

static uint32_t add_rational(int32_t numerator, int32_t denominator, uint8_t * buffer, uint32_t * data_offset)
{
    uint32_t result = *data_offset;
    *(int32_t*)(buffer + *data_offset) = numerator;
    *data_offset += sizeof(int32_t);
    *(int32_t*)(buffer + *data_offset) = denominator;
    *data_offset += sizeof(int32_t);
    return result;
}

static void add_ifd(struct directory_entry * ifd, uint8_t * header, size_t * position, int count)
{
    *(uint16_t*)(header + *position) = count;
    *position += sizeof(uint16_t);
    memcpy(header + *position, ifd, count * sizeof(struct directory_entry));
    *position += count * sizeof(struct directory_entry);
    *(uint32_t*)(header + *position) = 0;
    *position += sizeof(uint32_t);
}

static char * format_datetime(char * datetime, struct frame_headers * frame_headers)
{
    uint32_t seconds = frame_headers->rtci_hdr.tm_sec + (uint32_t)((frame_headers->vidf_hdr.timestamp - frame_headers->rtci_hdr.timestamp) / 1000000);
    uint32_t minutes = frame_headers->rtci_hdr.tm_min + seconds / 60;
    uint32_t hours = frame_headers->rtci_hdr.tm_hour + minutes / 60;
    uint32_t days = frame_headers->rtci_hdr.tm_mday + hours / 24;
    //TODO: days could also overflow in the month, but this is no longer simple modulo arithmetic like with hr:min:sec
    sprintf(datetime, "%04d:%02d:%02d %02d:%02d:%02d",
            1900 + frame_headers->rtci_hdr.tm_year,
            frame_headers->rtci_hdr.tm_mon,
            days,
            hours % 24,
            minutes % 60,
            seconds % 60);
    return datetime;
}

#define MATRIX_MULTIPLY(result,matA,matB,colsA,rowsA_colsB,rowsB) \
for(i=0;i<colsA;i++){\
    for(j=0;j<rowsB;j++){\
        result[i][j]=0;\
        for(k=0;k<rowsA_colsB;k++){\
            result[i][j]+=matA[i][k]*matB[k][j];\
        }\
    }\
}

#define MATRIX_INVERSE(result, matrix, n) \
for(i = 0; i < n; i++){\
    for(j = 0; j < n; j++){\
        result[i][j] = matrix[i][j];\
    }\
    for(j = n; j < 2*n; j++){\
        result[i][j] = i==(j-n) ? 1.0 : 0.0;\
    }\
}\
for(i = 0; i < n; i++){\
    for(j = 0; j < n; j++){\
        if(i!=j){\
            a = result[j][i]/result[i][i];\
            for(k = 0; k < 2*n; k++){\
                result[j][k] -= a * result[i][k];\
            }\
        }\
    }\
}\
for(i = 0; i < n; i++){\
    a = result[i][i];\
    for(j = 0; j < 2*n; j++){\
        result[i][j] /= a;\
    }\
}\
for(i = 0; i < n; i++){\
    for(j = 0; j < n; j++){\
        result[i][j] = result[i][j + n];\
    }\
}

static void kelvin_to_xyz(float kelvin, float xyz[3][1])
{
    double x = 0;
    double y = 0;
    
    x = -4.607e9 / pow(kelvin, 3) + 2.9678e6 / pow(kelvin, 2) + 0.09911e3 / kelvin + 0.244063;
    y = -3 * pow(x, 2) + 2.870 * x - 0.275;
    
    xyz[0][0] = (float)(x / y);
    xyz[1][0] = 1.0f;
    xyz[2][0] = (float)((1 - x - y) / y);
}

//TODO: this is still wrong, I have little to no idea how this is supposed to work
static void kelvin_to_rgb(int kelvin, int gm, int32_t *wbal, int32_t *color_matrix)
{
    static const float rgb_to_xyz[3][3] = {
        {0.412456, 0.357576, 0.180437},
        {0.212673, 0.715152, 0.072175},
        {0.0193339, 0.119192, 0.950304}
    };
    static const float xyz_bradford[3][3] = {
        { 0.8951000,  0.2664000, -0.1614000},
        {-0.7502000,  1.7135000,  0.0367000},
        { 0.0389000, -0.0685000,  1.0296000}
    };
    
    int i,j,k;
    float a;
    float dst[3][1];
    float src[3][1];
    float xyz_kelvin[3][1];
    float xyz_d65[3][1];
    float xyz_scale[3][3];
    float temp[3][3];
    float temp2[3][3];
    float xyz_kelvin_wb[3][3];
    float neutral[3][1] = {{(float)wbal[0] / (float)wbal[1]}, {(float)wbal[2] / (float)wbal[3]}, {(float)wbal[4] / (float)wbal[5]}};
    float cam_matrix[3][3] = {
        {(float)color_matrix[0] / (float)color_matrix[1], (float)color_matrix[2] / (float)color_matrix[3], (float)color_matrix[4] / (float)color_matrix[5]},
        {(float)color_matrix[6] / (float)color_matrix[7], (float)color_matrix[8] / (float)color_matrix[9], (float)color_matrix[10] / (float)color_matrix[11]},
        {(float)color_matrix[12] / (float)color_matrix[13], (float)color_matrix[14] / (float)color_matrix[15], (float)color_matrix[16] / (float)color_matrix[17]}
    };
    float result[3][1];
    float cam_matrix_inverse[3][6];
    float xyz_to_rgb[3][6];
    float xyz_bradford_inverse[3][6];
    
    memset(xyz_scale, 0, sizeof(xyz_scale));
    memset(dst, 0, sizeof(dst));
    memset(src, 0, sizeof(src));
    memset(temp, 0, sizeof(temp));
    memset(temp2, 0, sizeof(temp2));
    memset(cam_matrix_inverse, 0, sizeof(cam_matrix_inverse));
    memset(xyz_to_rgb, 0, sizeof(xyz_to_rgb));
    memset(xyz_bradford_inverse, 0, sizeof(xyz_bradford_inverse));
    
    kelvin_to_xyz(kelvin, xyz_kelvin);
    kelvin_to_xyz(6500, xyz_d65);
    
    MATRIX_MULTIPLY(src, xyz_bradford, xyz_d65, 3, 3, 1);
    MATRIX_MULTIPLY(dst, xyz_bradford, xyz_kelvin, 3, 3, 1);
    
    xyz_scale[0][0] = dst[0][0] / src[0][0];
    xyz_scale[1][1] = dst[1][0] / src[1][0];
    xyz_scale[2][2] = dst[2][0] / src[2][0];
    
    MATRIX_INVERSE(xyz_bradford_inverse, xyz_bradford, 3);
    
    MATRIX_MULTIPLY(temp, xyz_bradford_inverse, xyz_scale, 3, 3, 3);
    MATRIX_MULTIPLY(xyz_kelvin_wb, temp, xyz_bradford, 3, 3, 3);
    
    MATRIX_INVERSE(xyz_to_rgb, rgb_to_xyz, 3);
    MATRIX_INVERSE(cam_matrix_inverse, cam_matrix, 3);
    
    MATRIX_MULTIPLY(temp, xyz_to_rgb, xyz_kelvin_wb, 3, 3, 3);
    MATRIX_MULTIPLY(temp2, temp, cam_matrix_inverse, 3, 3, 3);
    MATRIX_MULTIPLY(result, temp2, neutral, 3, 3, 1);
    
    //convert to fixed point
    wbal[0] = result[0][0] * 100000;
    wbal[1] = 100000;
    wbal[2] = result[1][0] * 100000;
    wbal[3] = 100000;
    wbal[4] = result[2][0] * 100000;
    wbal[5] = 100000;
}

static void get_white_balance(mlv_wbal_hdr_t wbal_hdr, int32_t *wbal, int32_t *color_matrix)
{
    wbal[0] = wbal_hdr.wbgain_r; wbal[1] = wbal_hdr.wbgain_g;
    wbal[2] = wbal_hdr.wbgain_g; wbal[3] = wbal_hdr.wbgain_g;
    wbal[4] = wbal_hdr.wbgain_b; wbal[5] = wbal_hdr.wbgain_g;
    
    if(wbal_hdr.wb_mode == WB_AUTO || wbal_hdr.wb_mode == WB_KELVIN)
    {
        kelvin_to_rgb(wbal_hdr.kelvin, wbal_hdr.wbs_gm, wbal, color_matrix);
    }
    else if(wbal_hdr.wb_mode != WB_CUSTOM)
    {
        memcpy(wbal, daylight_wbal, sizeof(daylight_wbal));
    }
}

/**
 * Generates the CDNG header (or some section of it). The result is written into output_buffer.
 * @param frame_headers The MLV blocks associated with the frame
 * @return The size of the DNG header or 0 on failure
 */
size_t dng_get_header_data(struct frame_headers * frame_headers, uint8_t * output_buffer, off_t offset, size_t max_size)
{
    /*
    - build the tiff header in a buffer
    - then copy the buffer to the output buffer according to offset and max_size
    this shouldn't be a big performance hit and it's a lot easier than trying
    to only generate the requested section of the header (most of the time the
    entire header will be requested all at once anyway, since typically the 
    requested size is at least 64kB)
    */
    size_t header_size = dng_get_header_size(frame_headers);
    uint8_t * header = (uint8_t *)malloc(header_size);
    size_t position = 0;
    if(header)
    {
        memset(header, 0 , header_size);
        memcpy(header + position, tiff_header, sizeof(tiff_header));
        position += sizeof(tiff_header);
        char make[32];
        char * model = (char*)frame_headers->idnt_hdr.cameraName;
        if(!model) model = "???";
        //make is usually the first word of cameraName
        strncpy(make, model, 32);
        char * space = strchr(make, ' ');
        if(space) *space = 0x0;
        
        uint32_t exif_ifd_offset = (uint32_t)(position + sizeof(uint16_t) + IFD0_COUNT * sizeof(struct directory_entry) + sizeof(uint32_t));
        uint32_t data_offset = exif_ifd_offset + sizeof(uint16_t) + EXIF_IFD_COUNT * sizeof(struct directory_entry) + sizeof(uint32_t);
        
        int bpp = frame_headers->rawi_hdr.raw_info.bits_per_pixel;
        //we get the active area of the original raw source, not the recorded data, so overwrite the active area if the recorded data does
        //not contain the OB areas
        if(frame_headers->rawi_hdr.xRes < frame_headers->rawi_hdr.raw_info.active_area.x1 + frame_headers->rawi_hdr.raw_info.active_area.x2 ||
           frame_headers->rawi_hdr.yRes < frame_headers->rawi_hdr.raw_info.active_area.y1 + frame_headers->rawi_hdr.raw_info.active_area.y2)
        {
            frame_headers->rawi_hdr.raw_info.active_area.x1 = 0;
            frame_headers->rawi_hdr.raw_info.active_area.y1 = 0;
            frame_headers->rawi_hdr.raw_info.active_area.x2 = frame_headers->rawi_hdr.xRes;
            frame_headers->rawi_hdr.raw_info.active_area.y2 = frame_headers->rawi_hdr.yRes;
        }
        int32_t frame_rate[2] = {frame_headers->file_hdr.sourceFpsNom, frame_headers->file_hdr.sourceFpsDenom};
        char datetime[255];
        int32_t basline_exposure[2] = {frame_headers->rawi_hdr.raw_info.exposure_bias[0],frame_headers->rawi_hdr.raw_info.exposure_bias[1]};
        if(basline_exposure[1] == 0)
        {
            basline_exposure[0] = 0;
            basline_exposure[1] = 1;
        }
        
        int32_t wbal[6];
        get_white_balance(frame_headers->wbal_hdr, wbal, frame_headers->rawi_hdr.raw_info.color_matrix1);
        
        struct directory_entry IFD0[IFD0_COUNT] =
        {
            {tcNewSubFileType,              ttLong,     1,      sfMainImage},
            {tcImageWidth,                  ttLong,     1,      frame_headers->rawi_hdr.xRes},
            {tcImageLength,                 ttLong,     1,      frame_headers->rawi_hdr.yRes},
            {tcBitsPerSample,               ttShort,    1,      16},
            {tcCompression,                 ttShort,    1,      ccUncompressed},
            {tcPhotometricInterpretation,   ttShort,    1,      piCFA},
            {tcFillOrder,                   ttShort,    1,      1},
            {tcMake,                        ttAscii,    STRING_ENTRY(make, header, &data_offset)},
            {tcModel,                       ttAscii,    STRING_ENTRY(model, header, &data_offset)},
            {tcStripOffsets,                ttLong,     1,      (uint32_t)header_size},
            {tcOrientation,                 ttShort,    1,      1},
            {tcSamplesPerPixel,             ttShort,    1,      1},
            {tcRowsPerStrip,                ttShort,    1,      frame_headers->rawi_hdr.yRes},
            {tcStripByteCounts,             ttLong,     1,      (uint32_t)dng_get_image_size(frame_headers)},
            {tcPlanarConfiguration,         ttShort,    1,      pcInterleaved},
            {tcSoftware,                    ttAscii,    STRING_ENTRY(MLVFS_SOFTWARE_NAME, header, &data_offset)},
            {tcDateTime,                    ttAscii,    STRING_ENTRY(format_datetime(datetime,frame_headers), header, &data_offset)},
            {tcCFARepeatPatternDim,         ttShort,    2,      0x00020002}, //2x2
            {tcCFAPattern,                  ttByte,     4,      0x02010100}, //RGGB
            {tcExifIFD,                     ttLong,     1,      exif_ifd_offset},
            {tcDNGVersion,                  ttByte,     4,      0x00000401}, //1.4.0.0 in little endian
            {tcUniqueCameraModel,           ttAscii,    STRING_ENTRY(model, header, &data_offset)},
            {tcLinearizationTable,          ttShort,    LINEARIZATION_TABLE((1 << bpp) - 1, header, &data_offset)},
            {tcBlackLevel,                  ttLong,     1,      frame_headers->rawi_hdr.raw_info.black_level},
            {tcWhiteLevel,                  ttLong,     1,      frame_headers->rawi_hdr.raw_info.white_level},
            {tcDefaultCropOrigin,           ttShort,    2,      PACK(frame_headers->rawi_hdr.raw_info.crop.origin)},
            {tcDefaultCropSize,             ttShort,    2,      PACK2(frame_headers->rawi_hdr.xRes,frame_headers->rawi_hdr.yRes)},
            {tcColorMatrix1,                ttSRational,RATIONAL_ENTRY(frame_headers->rawi_hdr.raw_info.color_matrix1, header, &data_offset, 18)},
            {tcAsShotNeutral,               ttRational, RATIONAL_ENTRY(wbal, header, &data_offset, 6)},
            {tcBaselineExposure,            ttSRational,RATIONAL_ENTRY(basline_exposure, header, &data_offset, 2)},
            {tcCalibrationIlluminant1,      ttShort,    1,      lsD65},
            {tcActiveArea,                  ttLong,     ARRAY_ENTRY(frame_headers->rawi_hdr.raw_info.dng_active_area, header, &data_offset, 4)},
            {tcFrameRate,                   ttSRational,RATIONAL_ENTRY(frame_rate, header, &data_offset, 2)},
            {tcBaselineExposureOffset,      ttSRational,RATIONAL_ENTRY2(0, 1, header, &data_offset)},
        };
        
        struct directory_entry EXIF_IFD[EXIF_IFD_COUNT] =
        {
            {tcExposureTime,                ttRational, RATIONAL_ENTRY2((int32_t)frame_headers->expo_hdr.shutterValue/1000, 1000, header, &data_offset)},
            {tcFNumber,                     ttRational, RATIONAL_ENTRY2(frame_headers->lens_hdr.aperture, 100, header, &data_offset)},
            {tcISOSpeedRatings,             ttShort,    1,      frame_headers->expo_hdr.isoValue},
            {tcSensitivityType,             ttShort,    1,      stISOSpeed},
            {tcExifVersion,                 ttUndefined,4,      0x30333230},
            {tcSubjectDistance,             ttRational, RATIONAL_ENTRY2(frame_headers->lens_hdr.focalDist, 1, header, &data_offset)},
            {tcFocalLength,                 ttRational, RATIONAL_ENTRY2(frame_headers->lens_hdr.focalLength, 1, header, &data_offset)},
            {tcLensModelExif,               ttAscii,    STRING_ENTRY((char*)frame_headers->lens_hdr.lensName, header, &data_offset)},
        };
        
        add_ifd(IFD0, header, &position, IFD0_COUNT);
        add_ifd(EXIF_IFD, header, &position, EXIF_IFD_COUNT);
        
        size_t output_size = MIN(max_size, header_size - (size_t)MIN(0, offset));
        if(output_size)
        {
            memcpy(output_buffer, header + offset, output_size);
        }
        free(header);
        return output_size;
    }
    return 0;
}

/**
 * Computes the size of the DNG header (all the IFDs, metadata, and whatnot). 
 * This is hardcoded to 64kB, which should be plenty large enough. This also
 * lines up with requests from FUSE, which are typically (but not always) 64kB
 * @param frame_headers The MLV blocks associated with the frame
 * @return The size of the DNG header
 */
size_t dng_get_header_size(struct frame_headers * frame_headers)
{
    return HEADER_SIZE;
}

/**
 * Unpacks bits to 16 bit little endian
 * @param frame_headers The MLV blocks associated with the frame
 * @param packed_bits A buffer containing the packed imaged data
 * @param output_buffer The buffer where the result will be written
 * @param offset The offset into the frame to read
 * @param max_size The size in bytes to write into the buffer
 * @return The number of bytes written (just max_size)
 */
size_t dng_get_image_data(struct frame_headers * frame_headers, uint16_t * packed_bits, uint8_t * output_buffer, off_t offset, size_t max_size)
{
    int bpp = frame_headers->rawi_hdr.raw_info.bits_per_pixel;
    uint64_t pixel_start_index = MAX(0, offset) / 2; //lets hope offsets are always even for now
    uint64_t pixel_start_address = pixel_start_index * bpp / 16;
    size_t output_size = max_size - (offset < 0 ? (size_t)(-offset) : 0);
    uint64_t pixel_count = output_size / 2;
    uint16_t * dng_data = (uint16_t *)(output_buffer + (offset < 0 ? (size_t)(-offset) : 0) + offset % 2);
    uint32_t mask = (1 << bpp) - 1;
    
	/* ok this is pointing outside the reserved buffer, but its indexed later to get within bounds again */
	uint16_t * raw_bits = (uint16_t *)(packed_bits - pixel_start_address);
    
    for(size_t pixel_index = 0; pixel_index < pixel_count; pixel_index++)
    {
        uint64_t bits_offset = (pixel_start_index + pixel_index) * bpp;
        uint64_t bits_address = bits_offset / 16;
        uint32_t bits_shift = bits_offset % 16;
        uint32_t data = (raw_bits[bits_address] << 16) | raw_bits[bits_address + 1];
        
        dng_data[pixel_index] = (uint16_t)((data >> ((32 - bpp) - bits_shift)) & mask);
    }
    return max_size;
}

/**
 * Computes the resulting, unpacked size of the actual image data for a CDNG (does not include header)
 * @param frame_headers The MLV blocks associated with the frame
 * @return The size of the actual image data
 */
size_t dng_get_image_size(struct frame_headers * frame_headers)
{
    return (frame_headers->vidf_hdr.blockSize - frame_headers->vidf_hdr.frameSpace - sizeof(mlv_vidf_hdr_t)) * 16 / frame_headers->rawi_hdr.raw_info.bits_per_pixel; //16 bit
}

/**
 * Returns the resulting size of the entire CDNG including the header
 * @param frame_headers The MLV blocks associated with the frame
 */
size_t dng_get_size(struct frame_headers * frame_headers)
{
    return dng_get_header_size(frame_headers) + dng_get_image_size(frame_headers);
}
