#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "plugin_interface.h"

#include "helpers.h"

static gboolean djvu_check_file(const gchar* filename)
{
  gchar* type = get_suffix(filename);
  if (!type)
    return FALSE;
  gboolean result = !strcmp(type, "djvu");
  g_free(type);
  return result;
}

static GData* djvu_get_metainfo(const gchar *filename)
{
  FILE* pipe;
  GIOChannel* channel;
  GData* result;
  gchar* cmdline;

  g_datalist_init(&result);
  
  {
    gchar* q = g_shell_quote(filename);
    cmdline = g_strdup_printf("djvused %s -e print-meta", q);
    g_free(q);
  }

  pipe = popen(cmdline, "r");
  if (!pipe)
    {
      g_free(cmdline);
      return result;
    }

  channel = g_io_channel_unix_new(fileno(pipe));
    
  while (TRUE)
    {
      gchar* line;
        
      if (G_IO_STATUS_NORMAL != g_io_channel_read_line(channel, &line, NULL, NULL, NULL))
	break;
      g_strchomp(line);
	
      gchar* val;
      
      val = strchr(line, '\t');
      if (val != NULL)
        {
	  *val = '\0';
	  ++val;
	  if (strchr(val, '\t') == NULL) /* there is one '\t' in line */
            {
	      g_datalist_set_data_full(&result, line, dequote(val, '"'), g_free);
            }
        }
        
      g_free(line);
    }
  
  g_io_channel_unref(channel);
  pclose(pipe);
  g_free(cmdline);
  
  return result;
}

static void print_metainfo(GQuark key_id, gpointer data, gpointer user_data)
{
  gchar* quoted = quote((gchar*)data, '"');
  fprintf((FILE*)user_data, "%s\t%s\n", g_quark_to_string(key_id), quoted);
  g_free(quoted);
}

static void djvu_set_metainfo(const gchar* filename, GData* metainfo)
{
  gchar tempfile[] = "/tmp/metainfo-XXXXXX";
  int fd = mkstemp(tempfile);
  if (fd < 0)
    return; // error

  FILE* f = fdopen(fd, "w");
  g_datalist_foreach(&metainfo, print_metainfo, f);
  fclose(f);

  gchar* cmdline;
  {
    gchar* qf = g_shell_quote(filename);

    gchar* cmd = g_strdup_printf("set-meta %s; save", tempfile);
    gchar* qc = g_shell_quote(cmd);
    g_free(cmd);
    
    cmdline = g_strdup_printf("djvused %s -e %s", qf, qc);
    g_free(qf);
    g_free(qc);
  }

  system(cmdline);
}

PluginInterface djvu_interface =
{
  djvu_check_file,
  djvu_get_metainfo,
  djvu_set_metainfo
};
