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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "webgui.h"
#include "mongoose.h"

static int halt_webgui = 0;
static struct mlvfs * mlvfs_config = NULL;

static const char * HTML =
"<html>"
"<head>"
"  <script src=\"http://code.jquery.com/jquery-1.11.0.min.js\"></script>"
"  <script> jQuery(function() {"
"    $.ajax({ url: '/get_value', dataType: 'json', success: function(d) {"
"      $('#dir').val(d.dir);"
"      $('input:radio[name=\"badpix\"][value=' + d.badpix + ']').prop('checked', true);"
"      $('input:radio[name=\"chroma_smooth\"][value=' + d.chroma_smooth + ']').prop('checked', true);"
"      $('input:radio[name=\"stripes\"][value=' + d.stripes + ']').prop('checked', true);"
"      $('input:radio[name=\"dual_iso\"][value=' + d.dual_iso + ']').prop('checked', true);"
"    }});"
"    $(document).on('click', '#button', function() {"
"      $.ajax({ url: '/set_value', dataType: 'json', "
"      data: { \"badpix\": $('input:radio[name=badpix]:checked').val(), \"chroma_smooth\": $('input:radio[name=chroma_smooth]:checked').val(), \"stripes\": $('input:radio[name=stripes]:checked').val(), \"dual_iso\": $('input:radio[name=dual_iso]:checked').val() } });"
"      return false; });"
"    });"
"  </script>"
"</head>"
"<body>"
"  <h1>MLVFS Configuration Options</h1>"
"  <form>"
"    Source Directory: <input type=text id=dir size=64 readonly/><br/><br/>"
"    Bad Pixel Fix: "
"    <input type=radio name=badpix value=0 >Off</input>"
"    <input type=radio name=badpix value=1 >On</input><br/><br/>"
"    Vertical Stripes Fix: "
"    <input type=radio name=stripes value=0 >Off</input>"
"    <input type=radio name=stripes value=1 >On</input><br/><br/>"
"    Chroma Smoothing: "
"    <input type=radio name=chroma_smooth value=0 >None</input>"
"    <input type=radio name=chroma_smooth value=2 >2x2</input>"
"    <input type=radio name=chroma_smooth value=3 >3x3</input>"
"    <input type=radio name=chroma_smooth value=5 >5x5</input><br/><br/>"
"    Dual ISO: "
"    <input type=radio name=dual_iso value=0 >Off</input>"
"    <input type=radio name=dual_iso value=1 >Preview</input><br/><br/>"
"    <input type=submit id=button value=Save />"
"  </form>"
"</body>"
"</html>";


static int webgui_handler(struct mg_connection *conn, enum mg_event ev)
{
    if (ev == MG_REQUEST)
    {
        if (strcmp(conn->uri, "/get_value") == 0)
        {
            mg_printf_data(conn,
                           "{\"dir\": \"%s\", \"badpix\": %d, \"chroma_smooth\": %d, \"stripes\": %d, \"dual_iso\": %d}",
                           mlvfs_config->mlv_path,
                           mlvfs_config->fix_bad_pixels,
                           mlvfs_config->chroma_smooth,
                           mlvfs_config->fix_stripes,
                           mlvfs_config->dual_iso);
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
            
            mg_printf_data(conn, "%s", "{\"success\": true}");
        }
        else
        {
            //send the HTML
            mg_send_header(conn, "Content-Type", "text/html");
            mg_send_header(conn, "Cache-Control", "max-age=0, post-check=0, pre-check=0, no-store, no-cache, must-revalidate");
            mg_printf_data(conn, "%s", HTML);
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
    mg_set_option(server, "listening_port", "8000");
    
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