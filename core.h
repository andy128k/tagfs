#ifndef CORE_H
#define CORE_H

#include <gtk/gtk.h>

GtkWidget* get_page(const gchar* filename, const gchar* mime, GError** error);

#endif
