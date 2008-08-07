#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "helpers.h"

gchar* quote(const gchar* string, gchar quote)
{
  gchar* result = g_malloc(strlen(string) * 2 + 3);
  gchar* iter = result;
  *iter++ = quote;
  for (; *string != '\0'; ++string)
    {
      if (*string == quote)
	*iter++ = '\\';
      *iter++ = *string;
    }
  *iter++ = quote;
  *iter = '\0';
  return result;
}

gchar* dequote(const gchar* string, gchar quote)
{
  gsize len = strlen(string);

  if (string[0] != quote || string[len-1] != quote)
    return g_strdup(string);

  gchar* result = g_malloc(len - 2 + 1);
  gchar* iter = result;
  int i;

  for (i = 1; i < len-1; ++i)
    {
      if (!(string[i] == '\\' && string[i+1] == quote))
	*iter++ = string[i];
    }
  *iter = '\0';
  return result;
}

gchar* get_suffix(const gchar* filename)
{
  gchar* basename = g_path_get_basename(filename);
  gchar* suffix = strrchr(basename, '.');
  gchar* result = NULL;
  if (suffix != NULL)
    result = g_strdup(suffix + 1);
  g_free(basename);
  return result;
}

struct datalists_compare
{
  GData* super;
  gboolean result;
};

static void datalist_contains_item(GQuark key_id, gpointer data1, gpointer user_data)
{
  struct datalists_compare* c = (struct datalists_compare*)user_data;
  if (c->result)
    {
      gpointer data2 = g_datalist_id_get_data(&c->super, key_id);
      if (data2 == NULL)
	c->result = FALSE;
      else
	c->result = !strcmp(data1, data2);
    }
}

gboolean are_datalists_equal(GData* data1, GData* data2)
{
  struct datalists_compare c;

  c.super = data2;
  c.result = TRUE;
  g_datalist_foreach(&data1, datalist_contains_item, &c);
  if (!c.result)
    return FALSE;

  c.super = data1;
  c.result = TRUE;
  g_datalist_foreach(&data2, datalist_contains_item, &c);
  return c.result;
}

gboolean exec_and_read_output(const gchar* cmdline, read_callback_t callback, void* user_data)
{
  FILE* pipe;
  GIOChannel* channel;
  
  pipe = popen(cmdline, "r");
  if (!pipe)
      return FALSE;

  channel = g_io_channel_unix_new(fileno(pipe));
    
  while (TRUE)
    {
      gchar* line;
        
      if (G_IO_STATUS_NORMAL != g_io_channel_read_line(channel, &line, NULL, NULL, NULL))
	break;

      g_strchomp(line);
      callback(user_data, line);
      g_free(line);
    }
  
  g_io_channel_unref(channel);
  pclose(pipe);
  
  return TRUE;
}
