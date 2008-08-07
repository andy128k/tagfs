#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "plugin_interface.h"

#include "helpers.h"

static gboolean pdf_check_file(const gchar* filename, const gchar* mime)
{
  return !strcmp(mime, "application/pdf");
}

/*
def get_pdf_metadata_old(filename):
    p = subprocess.Popen(('pdfinfo', filename), stdout=subprocess.PIPE, universal_newlines=True)
    lines = p.stdout.read().split('\n')
    p.wait()

    result = {}
    for line in lines:
        k = line.split(':', 1)
        if len(k) == 2:
            key = k[0].strip()
            val = k[1].strip()
            result[key] = val
    return result
*/

struct read_context
{
  gint state;
  gchar* key;
  GData* result;
};

static void read_callback(void* user_data, gchar* line)
{
  struct read_context* rc = (struct read_context*)user_data;

  if (rc->state == 0)
    {
      if (memcmp(line, "InfoKey: ", 9) == 0)
	{
	  rc->state = 1;
	  rc->key = g_strdup(line + 9);
	}
    }
  else
    {
      if (memcmp(line, "InfoValue: ", 11) == 0)
	{
	  rc->state = 0;
	  gchar* value = g_strdup(line + 11);

	  g_datalist_set_data_full(&rc->result, rc->key, value, g_free);
	  g_free(rc->key);
	  rc->key = NULL;
	}
    }
}

static GData* pdf_get_metainfo(const gchar* filename, GError** error)
{
  gchar* cmdline;

  {
    gchar* q = g_shell_quote(filename);
    cmdline = g_strdup_printf("pdftk %s dump_data output -", q);
    g_free(q);
  }

  struct read_context rc;
  rc.state = 0;
  rc.key = NULL;
  g_datalist_init(&rc.result);

  gboolean r = exec_and_read_output(cmdline, read_callback, &rc);
  g_free(cmdline);

  if (rc.key != NULL)
    g_free(rc.key);

  if (r)
    return rc.result;
  else
    return NULL;
}

static void print_metainfo(GQuark key_id, gpointer data, gpointer user_data)
{
  gchar* quoted = quote((gchar*)data, '"');
  fprintf((FILE*)user_data, "InfoKey: %s\n", g_quark_to_string(key_id));
  fprintf((FILE*)user_data, "InfoValue: %s\n", quoted);
  g_free(quoted);
}

static void pdf_set_metainfo(const gchar* filename, GData* metainfo)
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
    
    // "pdftk %s update_info %s output %s", filename, tempfile, new
    cmdline = g_strdup_printf("djvused %s -e %s", qf, qc);
    g_free(qf);
    g_free(qc);
  }

  system(cmdline);

    /*
    os.unlink(filename)
    os.rename(new, filename)
    */
}

PluginInterface pdf_interface =
{
  pdf_check_file,
  pdf_get_metainfo,
  pdf_set_metainfo
};
