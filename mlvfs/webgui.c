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

#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include "mlvfs.h"
#include "raw.h"
#include "mlv.h"
#include "wav.h"
#include "dng.h"
#include "index.h"
#include "resource_manager.h"
#include "webgui.h"
#include "mongoose/mongoose.h"

static int halt_webgui = 0;
static struct mlvfs * mlvfs_config = NULL;
#define HTML_SIZE 65535

static char * JQUERY = NULL;

static char * HTML = NULL;

static const char * TABLE_HEADER_NO_PREVIEW =
"<table><tr>"
"<th>Filename</th>"
"<th>Frames</th>"
"<th>Audio</th>"
"<th>Resolution</th>"
"<th>Framerate</th>"
"<th>Duration</th>"
"<th>Camera Model</th>"
"<th>Camera Serial</th>"
"<th>Lens Name</th>"
"<th>Date/Time</th>"
"<th>Shutter</th>"
"<th>ISO</th>"
"<th>Aperture</th>"
"</tr>";

static const char * TABLE_HEADER =
"<table><tr>"
"<th>Filename</th>"
"<th>Preview <input type=button onclick=\"$(this).hide(); $('img').each(function(){$(this).attr('src', $(this).attr('delayedsrc'));$(this).attr('width','100');});\" value=\"Load\"></input></th>"
"<th>Frames</th>"
"<th>Audio</th>"
"<th>Resolution</th>"
"<th>Framerate</th>"
"<th>Duration</th>"
"<th>Camera Model</th>"
"<th>Camera Serial</th>"
"<th>Lens Name</th>"
"<th>Date/Time</th>"
"<th>Shutter</th>"
"<th>ISO</th>"
"<th>Aperture</th>"
"</tr>";

static int load_resource(char** resource, const char * filename)
{
    if(*resource == NULL)
    {
        FILE * f = fopen(filename, "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            if(ferror(f))
            {
                int err = errno;
                fprintf(stderr, "load_resource: file error: %s\n", strerror(err));
                return 0;
            }
            size_t length = ftell(f);
            fseek(f, 0, SEEK_SET);
            *resource = calloc(length + 1, 1);
            if (*resource)
            {
                fread(*resource, 1, length, f);
                if(ferror(f))
                {
                    int err = errno;
                    fprintf(stderr, "load_resource: file error: %s\n", strerror(err));
                    free(*resource);
                    *resource = NULL;
                    return 0;
                }
            }
            else
            {
                fprintf(stderr, "load_resource: malloc error\n");
            }
            fclose(f);
        }
        else
        {
            static char buf[1000];
            fprintf(stderr, "load_resource: fopen error: %s cwd=%s\n", filename, (char*)getcwd(buf,1000));
            return 0;
        }
    }
    return  1;
}

static void webgui_generate_mlv_html(char * html, const char * path)
{
    char real_path[1024];
    char * temp = malloc(sizeof(char) * HTML_SIZE);
    sprintf(real_path, "%s%s", mlvfs_config->mlv_path, path);
    fprintf(stderr, "webgui: analyzing %s...\n", real_path);
    int frame_count = mlv_get_frame_count(real_path);
    snprintf(temp, HTML_SIZE, "<td>%d</td>", frame_count);
    strncat(html, temp, HTML_SIZE);
    snprintf(temp, HTML_SIZE, "<td>%s</td>", has_audio(real_path) ? "yes" : "no");
    strncat(html, temp, HTML_SIZE);
    struct frame_headers frame_headers;
    if(mlv_get_frame_headers(real_path, 0, &frame_headers))
    {
        int duration = frame_headers.file_hdr.sourceFpsNom == 0 ? 0 : frame_count * frame_headers.file_hdr.sourceFpsDenom / frame_headers.file_hdr.sourceFpsNom;
        float frame_rate = frame_headers.file_hdr.sourceFpsDenom == 0 ? 0 : (float)frame_headers.file_hdr.sourceFpsNom / (float)frame_headers.file_hdr.sourceFpsDenom;
        snprintf(temp, HTML_SIZE, "<td>%d x %d</td>", frame_headers.rawi_hdr.xRes, frame_headers.rawi_hdr.yRes);
        strncat(html, temp, HTML_SIZE);
        snprintf(temp, HTML_SIZE, "<td>%.3f</td>", frame_rate);
        strncat(html, temp, HTML_SIZE);
        snprintf(temp, HTML_SIZE, "<td>%02d:%02d</td>", duration / 60, duration % 60);
        strncat(html, temp, HTML_SIZE);
        snprintf(temp, HTML_SIZE, "<td>%s</td>", frame_headers.idnt_hdr.cameraName);
        strncat(html, temp, HTML_SIZE);
        snprintf(temp, HTML_SIZE, "<td>%s</td>", frame_headers.idnt_hdr.cameraSerial);
        strncat(html, temp, HTML_SIZE);
        snprintf(temp, HTML_SIZE, "<td>%s</td>", frame_headers.lens_hdr.lensName);
        strncat(html, temp, HTML_SIZE);
        snprintf(temp, HTML_SIZE, "<td>%d-%d-%d %02d:%02d:%02d</td>", 1900 + frame_headers.rtci_hdr.tm_year, frame_headers.rtci_hdr.tm_mon + 1, frame_headers.rtci_hdr.tm_mday, frame_headers.rtci_hdr.tm_hour, frame_headers.rtci_hdr.tm_min, frame_headers.rtci_hdr.tm_sec);
        strncat(html, temp, HTML_SIZE);
        snprintf(temp, HTML_SIZE, "<td>%dms</td>", (int)frame_headers.expo_hdr.shutterValue/1000);
        strncat(html, temp, HTML_SIZE);
        snprintf(temp, HTML_SIZE, "<td>%d</td>", frame_headers.expo_hdr.isoValue);
        strncat(html, temp, HTML_SIZE);
        snprintf(temp, HTML_SIZE, "<td>f/%.1f</td>", frame_headers.lens_hdr.aperture / 100.0);
        strncat(html, temp, HTML_SIZE);
    }
    free(temp);
}

static char * webgui_generate_row_html(const char * path)
{
    char * temp = malloc(sizeof(char) * (HTML_SIZE + 1));
    if (!temp)
    {
        return NULL;
    }
    char * html = malloc(sizeof(char) * (HTML_SIZE + 1));
    if (!html)
    {
        free(temp);
        return NULL;
    }
    const char *short_path = find_last_separator(path) ? find_last_separator(path) + 1 : path;
    snprintf(temp, HTML_SIZE, "<td><a href=\"%s\">%s</a></td>", path, short_path);
    strncpy(html, temp, HTML_SIZE);
    snprintf(temp, HTML_SIZE, "<td><img src=\"#\" delayedsrc=\"%s/_PREVIEW.gif\"/></td>", path);
    strncat(html, temp, HTML_SIZE);
    webgui_generate_mlv_html(html, path);
    free(temp);
    return html;
}

static char * webgui_generate_html(const char * path)
{
    char * temp = malloc(sizeof(char) * (HTML_SIZE + 1));
    char * html = malloc(sizeof(char) * (HTML_SIZE + 1));
    char real_path[1024];
    sprintf(real_path, "%s%s", mlvfs_config->mlv_path, path);
    fprintf(stderr, "webgui: scanning %s...\n", real_path);
    if(string_ends_with(path, ".MLV") || string_ends_with(path, ".mlv"))
    {
        snprintf(html, HTML_SIZE, "%s", TABLE_HEADER_NO_PREVIEW);
        const char *short_path = find_last_separator(path) ? find_last_separator(path) + 1 : path;
        snprintf(temp, HTML_SIZE, "<tr><td>%s</td>", short_path);
        strncat(html, temp, HTML_SIZE);
        webgui_generate_mlv_html(html, path);
        strncat(html, "</tr>", HTML_SIZE);
        strncat(html, "</table>", HTML_SIZE);
        snprintf(temp, HTML_SIZE, "<hr/><img src=\"%s/_PREVIEW.gif\"/>", path);
        strncat(html, temp, HTML_SIZE);
    }
    else
    {
        snprintf(html, HTML_SIZE, "%s", TABLE_HEADER);
        DIR * dir = opendir(real_path);
        if (dir != NULL)
        {
            struct dirent * child;
            int i = 0;
            
            while ((child = readdir(dir)) != NULL)
            {
                if(!string_ends_with(child->d_name, ".MLD") && strcmp(child->d_name, "..") && strcmp(child->d_name, "."))
                {
                    if(string_ends_with(child->d_name, ".MLV") || string_ends_with(child->d_name, ".mlv") || child->d_type == DT_DIR)
                    {
                        if(string_ends_with(child->d_name, ".MLV") || string_ends_with(child->d_name, ".mlv"))
                        {
                            snprintf(temp, HTML_SIZE, "<tr class=\"%s\" delayedsrc=\"%s_ROWDATA.html\"><td><a href=\"%s\">%s</a> (Loading...)</td></tr>", (i++ % 2 ? "delayedeven" : "delayedodd"), child->d_name, child->d_name, child->d_name);
                        }
                        else
                        {
                            snprintf(temp, HTML_SIZE, "<tr class=\"%s\"><td><a href=\"%s/\">%s</a></td><td colspan=13 /></tr>", (i++ % 2 ? "even" : "odd"), child->d_name, child->d_name);
                        }
                        strncat(html, temp, HTML_SIZE - strlen(html));
                    }
                    else if (child->d_type == DT_UNKNOWN) // If d_type is not supported on this filesystem
                    {
                        struct stat file_stat;
                        char real_file_path[1024];
                        sprintf(real_file_path, "%s/%s", real_path, child->d_name);
                        if ((stat(real_file_path, &file_stat) == 0) && S_ISDIR(file_stat.st_mode))
                        {
                            snprintf(temp, HTML_SIZE, "<tr class=\"%s\"><td><a href=\"%s/\">%s</a></td><td colspan=13 /></tr>", (i++ % 2 ? "even" : "odd"), child->d_name, child->d_name);
                            strncat(html, temp, HTML_SIZE - strlen(html));
                        }
                    }
                }
            }
            closedir(dir);
        }
        strncat(html, "</table>", HTML_SIZE - strlen(html));
    }
    free(temp);
    return html;
}

static int webgui_handler(struct mg_connection *conn, enum mg_event ev)
{
    if (ev == MG_REQUEST)
    {
        if (strcmp(conn->uri, "/get_value") == 0)
        {
			mg_send_header(conn, "Content-Type", "application/json");
            mg_printf_data(conn,
                           "{\"fps\": \"%f\", \"deflicker\": \"%d\", \"name_scheme\": %d, \"badpix\": %d, \"chroma_smooth\": %d, \"stripes\": %d, \"fix_pattern_noise\": %d, \"dual_iso\": %d, \"hdr_interpolation_method\": %d, \"hdr_no_alias_map\": %d, \"hdr_no_fullres\": %d}",
                           mlvfs_config->fps,
                           mlvfs_config->deflicker,
                           mlvfs_config->name_scheme,
                           mlvfs_config->fix_bad_pixels,
                           mlvfs_config->chroma_smooth,
                           mlvfs_config->fix_stripes,
                           mlvfs_config->fix_pattern_noise,
                           mlvfs_config->dual_iso,
                           mlvfs_config->hdr_interpolation_method,
                           mlvfs_config->hdr_no_alias_map,
                           mlvfs_config->hdr_no_fullres);
        }
        else if (strcmp(conn->uri, "/set_value") == 0)
        {
            // This Ajax endpoint sets the new value for the device variable
            char buf[100] = "";
            mg_get_var(conn, "fps", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->fps = atof(buf);
            
            mg_get_var(conn, "deflicker", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->deflicker = atof(buf);
            
            mg_get_var(conn, "name_scheme", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->name_scheme = atoi(buf);
            
            mg_get_var(conn, "badpix", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->fix_bad_pixels = atoi(buf);
            
            mg_get_var(conn, "chroma_smooth", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->chroma_smooth = atoi(buf);
            
            mg_get_var(conn, "stripes", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->fix_stripes = atoi(buf);
            
            mg_get_var(conn, "fix_pattern_noise", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->fix_pattern_noise = atoi(buf);
            
            mg_get_var(conn, "dual_iso", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->dual_iso = atoi(buf);
            
            mg_get_var(conn, "hdr_interpolation_method", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->hdr_interpolation_method = atoi(buf);
            
            mg_get_var(conn, "hdr_no_alias_map", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->hdr_no_alias_map = atoi(buf);
            
            mg_get_var(conn, "hdr_no_fullres", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->hdr_no_fullres = atoi(buf);
            
            mg_printf_data(conn, "%s", "{\"success\": true}");
        }
        else if (strcmp(conn->uri, "/jquery-1.12.0.min.js") == 0)
        {
            if (load_resource(&JQUERY, "jquery-1.12.0.min.js"))
            {
				mg_send_header(conn, "Content-Type", "text/javascript");
                mg_printf_data(conn, "%s", JQUERY);
            }
        }
        else if(string_ends_with(conn->uri, "_ROWDATA.html"))
        {
            char * path = malloc((strlen(conn->uri) + 1 )* sizeof(char));
            if (!path)
            {
                return MG_FALSE;
            }
            strcpy(path, conn->uri);
            path[strlen(path) - strlen("_ROWDATA.html")] = 0x0;
            char * html = webgui_generate_row_html(path);
            if(html)
            {
                mg_printf_data(conn, "%s", html);
                free(html);
            }
            free(path);
        }
        else if(string_ends_with(conn->uri, "_PREVIEW.gif"))
        {
            int was_created;
            struct image_buffer * image_buffer = get_or_create_image_buffer(conn->uri, &create_preview, &was_created);
            mg_send_header(conn, "Content-Type", "image/gif");
            mg_send_data(conn, image_buffer->data, (int)image_buffer->size);
            release_image_buffer(image_buffer);
        }
        else
        {
            char * html = webgui_generate_html(conn->uri);
            if(html)
            {
                mg_send_header(conn, "Content-Type", "text/html");
                mg_send_header(conn, "Cache-Control", "max-age=0, post-check=0, pre-check=0, no-store, no-cache, must-revalidate");
                
                if(load_resource(&HTML, "html_template.html"))
                {
                    mg_printf_data(conn, HTML, conn->uri, mlvfs_config->mlv_path, conn->uri, html, VERSION, BUILD_DATE);
                }
                free(html);
            }
        }
        return MG_TRUE;
    }
    else if (ev == MG_AUTH)
    {
        return MG_TRUE;
    }
    
    return MG_FALSE;
}

static void *webgui_run(void *unused)
{
    struct mg_server *server;
    
    // Create and configure the server
    server = mg_create_server(NULL, webgui_handler);
    mg_set_option(server, "listening_port", mlvfs_config->port != NULL && strlen(mlvfs_config->port) > 0 ? mlvfs_config->port : "8000");
    
    while(!halt_webgui)
    {
        mg_poll_server(server, 1000);
    }
    pthread_exit(NULL);
}

void webgui_start(struct mlvfs * mlvfs)
{
    halt_webgui = 0;
    mlvfs_config = mlvfs;
    pthread_t thread;
    pthread_create(&thread, NULL, webgui_run, NULL);
}

void webgui_stop(void)
{
    halt_webgui = 1;
    if(JQUERY) free(JQUERY);
    if(HTML) free(HTML);
}
