#ifndef HELPERS_H
#define HELPERS_H

#include <glib.h>

gchar* quote(const gchar* string, gchar quote);
gchar* dequote(const gchar* string, gchar quote);

gchar* get_suffix(const gchar* filename);

gboolean are_datalists_equal(GData* data1, GData* data2);

typedef void (*read_callback_t)(void* user_data, gchar* line);
gboolean exec_and_read_output(const gchar* cmdline, read_callback_t callback, void* user_data);

#endif
