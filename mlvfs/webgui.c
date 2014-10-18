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
#include "mlvfs.h"
#include "raw.h"
#include "mlv.h"
#include "wav.h"
#include "dng.h"
#include "index.h"
#include "webgui.h"
#include "mongoose.h"

static int halt_webgui = 0;
static struct mlvfs * mlvfs_config = NULL;
#define HTML_SIZE 65536

static const char * HTML =
"<html>"
"<head>"
"  <title>MLVFS: %s</title>"
"  <style>"
"    body { font-family: \"Trebuchet MS\", Arial, Helvetica, sans-serif; }"
"    h1, h2, h3 { margin: 8px 6px 4px 6px;}"
"    table { border-collapse: collapse; font-size: 0.8em; }"
"    td, th { border: 1px solid #369; padding: 3px 7px 2px 7px; }"
"    th { text-align: left; padding-top: 5px; padding-bottom: 4px; background-color: #48D; color: #FFF; }"
"    tr.odd td { color: #000; background-color: #EEF; }"
"  </style>"
"  <script src=\"http://code.jquery.com/jquery-1.11.0.min.js\"></script>"
"  <script> jQuery(function() {"
"    $.ajax({ url: '/get_value', dataType: 'json', success: function(d) {"
"      $('#dir').val(d.dir);"
"      $('input:radio[name=\"badpix\"][value=' + d.badpix + ']').prop('checked', true);"
"      $('input:radio[name=\"chroma_smooth\"][value=' + d.chroma_smooth + ']').prop('checked', true);"
"      $('input:radio[name=\"stripes\"][value=' + d.stripes + ']').prop('checked', true);"
"      $('input:radio[name=\"dual_iso\"][value=' + d.dual_iso + ']').prop('checked', true);"
"      $('input:radio[name=\"hdr_interpolation_method\"][value=' + d.hdr_interpolation_method + ']').prop('checked', true);"
"      $('input:radio[name=\"hdr_no_alias_map\"][value=' + d.hdr_no_alias_map + ']').prop('checked', true);"
"      $('input:radio[name=\"hdr_no_fullres\"][value=' + d.hdr_no_fullres + ']').prop('checked', true);"
"      if(d.dual_iso == 2){ $('#hdr_interpolation_method').show(); $('#hdr_no_alias_map').show(); $('#hdr_no_fullres').show(); }"
"      else { $('#hdr_interpolation_method').hide(); $('#hdr_no_alias_map').hide(); $('#hdr_no_fullres').hide(); }"
"    }});"
"    $(document).on('click', 'input:radio', function() {"
"      $.ajax({ url: '/set_value', dataType: 'json', "
"      data: { "
"        \"badpix\": $('input:radio[name=badpix]:checked').val(), "
"        \"chroma_smooth\": $('input:radio[name=chroma_smooth]:checked').val(), "
"        \"stripes\": $('input:radio[name=stripes]:checked').val(), "
"        \"dual_iso\": $('input:radio[name=dual_iso]:checked').val(), "
"        \"hdr_interpolation_method\": $('input:radio[name=hdr_interpolation_method]:checked').val(), "
"        \"hdr_no_alias_map\": $('input:radio[name=hdr_no_alias_map]:checked').val(), "
"        \"hdr_no_fullres\": $('input:radio[name=hdr_no_fullres]:checked').val() "
"      } });"
"      if($('input:radio[name=dual_iso]:checked').val() == 2){ $('#hdr_interpolation_method').show(); $('#hdr_no_alias_map').show(); $('#hdr_no_fullres').show(); }"
"      else { $('#hdr_interpolation_method').hide(); $('#hdr_no_alias_map').hide(); $('#hdr_no_fullres').hide(); }"
"      return true; });"
"    });"
"  </script>"
"</head>"
"<body>"
"  <h1>MLVFS - Magic Lantern Video File System</h1>"
"  <hr/>"
"  <form>"
"    <table>"
"      <tr><th colspan=2>Configuration Options</th></tr>"
"      <tr>"
"        <td>Source Directory</td>"
"        <td><input type=text id=dir size=64 readonly/></td>"
"      </tr>"
"      <tr class=odd>"
"        <td>Bad Pixel Fix</td>"
"        <td><input type=radio name=badpix value=0 >Off</input><input type=radio name=badpix value=1 >On</input><input type=radio name=badpix value=2 >Aggressive</input></td>"
"      </tr>"
"      <tr>"
"        <td>Vertical Stripes Fix</td>"
"        <td><input type=radio name=stripes value=0 >Off</input><input type=radio name=stripes value=1 >On</input></td>"
"      </tr>"
"      <tr class=odd>"
"        <td>Chroma Smoothing</td>"
"        <td><input type=radio name=chroma_smooth value=0 >None</input><input type=radio name=chroma_smooth value=2 >2x2</input><input type=radio name=chroma_smooth value=3 >3x3</input><input type=radio name=chroma_smooth value=5 >5x5</input></td>"
"      </tr>"
"      <tr>"
"        <td>Dual ISO</td>"
"        <td><input type=radio name=dual_iso value=0 >Off</input><input type=radio name=dual_iso value=1 >Preview</input><input type=radio name=dual_iso value=2 >Full (20bit)</input></td>"
"      </tr>"
"      <tr class=odd id=hdr_interpolation_method >"
"        <td> Interpolation</td>"
"        <td><input type=radio name=hdr_interpolation_method value=0 >AMaZE</input><input type=radio name=hdr_interpolation_method value=1 >mean32</input></td>"
"      </tr>"
"      <tr id=hdr_no_alias_map >"
"        <td> Alias Map</td>"
"        <td><input type=radio name=hdr_no_alias_map value=1 >Off</input><input type=radio name=hdr_no_alias_map value=0 >On</input></td>"
"      </tr>"
"      <tr class=odd id=hdr_no_fullres >"
"        <td> Fullres Blending</td>"
"        <td><input type=radio name=hdr_no_fullres value=1 >Off</input><input type=radio name=hdr_no_fullres value=0 >On</input></td>"
"      </tr>"
"    </table>"
"  </form>"
"  <hr/>"
"  <h3>%s%s</h3>"
"  %s"
"</body>"
"</html>";

static const char * TABLE_HEADER =
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

static void webgui_generate_mlv_html(char * html, const char * path)
{
    char real_path[1024];
    sprintf(real_path, "%s%s", mlvfs_config->mlv_path, path);
    int frame_count = mlv_get_frame_count(real_path);
    snprintf(html, HTML_SIZE, "%s<td>%d</td>", html, frame_count);
    snprintf(html, HTML_SIZE, "%s<td>%s</td>", html, has_audio(real_path) ? "yes" : "no");
    struct frame_headers frame_headers;
    char dng_path[1024];
    sprintf(dng_path, "%s/0.dng", path);
    if(mlv_get_frame_headers(dng_path, 0, &frame_headers))
    {
        int duration = frame_count * frame_headers.file_hdr.sourceFpsDenom / frame_headers.file_hdr.sourceFpsNom;
        snprintf(html, HTML_SIZE, "%s<td>%d x %d</td>", html, frame_headers.rawi_hdr.xRes, frame_headers.rawi_hdr.yRes);
        snprintf(html, HTML_SIZE, "%s<td>%.3f</td>", html, (float)frame_headers.file_hdr.sourceFpsNom / (float)frame_headers.file_hdr.sourceFpsDenom);
        snprintf(html, HTML_SIZE, "%s<td>%02d:%02d</td>", html, duration / 60, duration % 60);
        snprintf(html, HTML_SIZE, "%s<td>%s</td>", html, frame_headers.idnt_hdr.cameraName);
        snprintf(html, HTML_SIZE, "%s<td>%s</td>", html, frame_headers.idnt_hdr.cameraSerial);
        snprintf(html, HTML_SIZE, "%s<td>%s</td>", html, frame_headers.lens_hdr.lensName);
        snprintf(html, HTML_SIZE, "%s<td>%d-%d-%d %02d:%02d:%02d</td>", html, 1900 + frame_headers.rtci_hdr.tm_year, frame_headers.rtci_hdr.tm_mon, frame_headers.rtci_hdr.tm_mday, frame_headers.rtci_hdr.tm_hour, frame_headers.rtci_hdr.tm_min, frame_headers.rtci_hdr.tm_sec);
        snprintf(html, HTML_SIZE, "%s<td>%dms</td>", html, (int)frame_headers.expo_hdr.shutterValue/1000);
        snprintf(html, HTML_SIZE, "%s<td>%d</td>", html, frame_headers.expo_hdr.isoValue);
        snprintf(html, HTML_SIZE, "%s<td>f/%.1f</td>", html, frame_headers.lens_hdr.aperture / 100.0);
    }
}

static char * webgui_generate_html(const char * path)
{
    char * html = malloc(sizeof(char) * HTML_SIZE);
    snprintf(html, HTML_SIZE, "%s", TABLE_HEADER);
    char real_path[1024];
    sprintf(real_path, "%s%s", mlvfs_config->mlv_path, path);
    if(string_ends_with(path, ".MLV") || string_ends_with(path, ".mlv"))
    {
        snprintf(html, HTML_SIZE, "%s<tr><td>%s</td>", html, path);
        webgui_generate_mlv_html(html, path);
        snprintf(html, HTML_SIZE, "%s</tr>", html);
    }
    else
    {
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
                        snprintf(html, HTML_SIZE, "%s<tr class=\"%s\"><td><a href=\"%s/%s\">%s</a></td>", html, (i++ % 2 ? "even" : "odd"), path + 1, child->d_name, child->d_name);
                        if(string_ends_with(child->d_name, ".MLV") || string_ends_with(child->d_name, ".mlv"))
                        {
                            char child_path[1024];
                            sprintf(child_path, "%s/%s", path, child->d_name);
                            webgui_generate_mlv_html(html, child_path);
                        }
                        else
                        {
                            snprintf(html, HTML_SIZE, "%s<td colspan=12 />", html);
                        }
                        snprintf(html, HTML_SIZE, "%s</tr>", html);
                    }
                    else if (child->d_type == DT_UNKNOWN) // If d_type is not supported on this filesystem
                    {
                        struct stat file_stat;
                        char real_file_path[1024];
                        sprintf(real_file_path, "%s/%s", real_path, child->d_name);
                        if ((stat(real_file_path, &file_stat) == 0) && S_ISDIR(file_stat.st_mode))
                        {
                            snprintf(html, HTML_SIZE, "%s<tr class=\"%s\"><td><a href=\"%s/%s\">%s</a></td><td colspan=12 /></tr>", html, (i++ % 2 ? "even" : "odd"), path + 1, child->d_name, child->d_name);
                        }
                    }
                }
            }
            closedir(dir);
        }
    }
    snprintf(html, HTML_SIZE, "%s</table>", html);
    return html;
}

static int webgui_handler(struct mg_connection *conn, enum mg_event ev)
{
    if (ev == MG_REQUEST)
    {
        if (strcmp(conn->uri, "/get_value") == 0)
        {
            mg_printf_data(conn,
                           "{\"dir\": \"%s\", \"badpix\": %d, \"chroma_smooth\": %d, \"stripes\": %d, \"dual_iso\": %d, \"hdr_interpolation_method\": %d, \"hdr_no_alias_map\": %d, \"hdr_no_fullres\": %d}",
                           mlvfs_config->mlv_path,
                           mlvfs_config->fix_bad_pixels,
                           mlvfs_config->chroma_smooth,
                           mlvfs_config->fix_stripes,
                           mlvfs_config->dual_iso,
                           mlvfs_config->hdr_interpolation_method,
                           mlvfs_config->hdr_no_alias_map,
                           mlvfs_config->hdr_no_fullres);
        }
        else if (strcmp(conn->uri, "/set_value") == 0)
        {
            // This Ajax endpoint sets the new value for the device variable
            char buf[100] = "";
            mg_get_var(conn, "badpix", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->fix_bad_pixels = atoi(buf);
            
            mg_get_var(conn, "chroma_smooth", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->chroma_smooth = atoi(buf);
            
            mg_get_var(conn, "stripes", buf, sizeof(buf));
            if(strlen(buf) > 0) mlvfs_config->fix_stripes = atoi(buf);
            
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
        else
        {
            char * html = webgui_generate_html(conn->uri);
            if(html)
            {
                mg_send_header(conn, "Content-Type", "text/html");
                mg_send_header(conn, "Cache-Control", "max-age=0, post-check=0, pre-check=0, no-store, no-cache, must-revalidate");
                
                mg_printf_data(conn, HTML, conn->uri, mlvfs_config->mlv_path, conn->uri, html);
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
}