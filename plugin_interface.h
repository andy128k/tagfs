#ifndef PLUGIN_INTERFACE_H
#define PLUGIN_INTERFACE_H

#include <glib.h>

typedef struct tagPluginInterface
{
  gboolean (*check_file)(const gchar* filename, const gchar* mime);
  GData* (*get_metainfo)(const gchar* filename, GError** error);
  void (*set_metainfo)(const gchar* filename, GData* metainfo);
} PluginInterface;

#endif

