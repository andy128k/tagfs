#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include "core.h"

int main(int argc, char** argv)
{
  gtk_init(&argc, &argv);

  if (argc != 2)
    {
      fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
      exit(1);
    }

  const gchar* filename = argv[1];

  {
    struct stat st;
    stat(filename, &st);
    if (!S_ISREG(st.st_mode))
      {
	fprintf(stderr, "Error: %s is not a file\n", filename);
	exit(1);
      }
  }

  GtkWindow* window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  gtk_window_set_position(window, GTK_WIN_POS_CENTER);
  gtk_window_set_default_size(window, 400, 300);
  g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  GtkWidget* page = get_page(filename);
  if (page == NULL)
    {
      fprintf(stderr, "Error: Can't create page.\n");
      exit(1);
    }

  gtk_container_add(GTK_CONTAINER(window), page);
  
  gtk_widget_show_all(GTK_WIDGET(window));
  gtk_main();
      
  return 0;
}
