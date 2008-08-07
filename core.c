#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <magic.h>

#include "core.h"

#include "helpers.h"
#include "plugin_interface.h"

extern PluginInterface djvu_interface;
extern PluginInterface pdf_interface;

static PluginInterface* s_plugins[] = {
  &djvu_interface,
  &pdf_interface
};
#define PLUGINS_COUNT (sizeof(s_plugins)/sizeof(*s_plugins))

static gboolean question(const gchar* text)
{
  gboolean result;
  GtkWidget* d = gtk_message_dialog_new(NULL,
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_QUESTION,
					GTK_BUTTONS_YES_NO,
					"%s",
					text);
  result = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
  gtk_object_destroy(GTK_OBJECT(d));
  return result;
}

typedef struct tagState
{
  PluginInterface* plugin;
  gchar* filename;
  GData* metainfo;
  GtkListStore* store;
} state_t;

static void add_clicked(GtkWidget* button, gpointer user_data)
{
  GtkWidget* dlg = gtk_dialog_new_with_buttons("Add entry",
					       GTK_WINDOW(gtk_widget_get_toplevel(button)),
					       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					       GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT,
					       NULL);
  gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
 
  GtkTable* table = GTK_TABLE(gtk_table_new(2, 2, FALSE));
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dlg)->vbox), GTK_WIDGET(table));

  gtk_object_set(GTK_OBJECT(table),
		 "border-width", 8,
		 "row-spacing", 4,
		 "column-spacing", 4,
		 NULL);

  GtkWidget* lk = gtk_label_new("Key");
  gtk_misc_set_alignment(GTK_MISC(lk), 0, 0.5);
  gtk_table_attach(table, lk, 0, 1, 0, 1, GTK_FILL, 0, 0, 0);

  GtkWidget* ek = gtk_entry_new();
  gtk_table_attach_defaults(table, ek, 1, 2, 0, 1);

  GtkWidget* lv = gtk_label_new("Value");
  gtk_misc_set_alignment(GTK_MISC(lv), 0, 0.5);
  gtk_table_attach(table, lv, 0, 1, 1, 2, GTK_FILL, 0, 0, 0);

  GtkWidget* ev = gtk_entry_new();
  gtk_table_attach_defaults(table, ev, 1, 2, 1, 2);

  gtk_widget_show_all(GTK_WIDGET(dlg));

  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT)
    {
      gchar* k = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(ek))));
      gchar* v = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(ev))));
      if (k && k[0] != '\0')
	{
	  GtkListStore* store = GTK_LIST_STORE(user_data);

	  GtkTreeIter iter;
	  gtk_list_store_append(store, &iter);
	  gtk_list_store_set(store, &iter,
			     0, k,
			     1, v,
			     -1);
	}
      g_free(k);
      g_free(v);
    }
  gtk_object_destroy(GTK_OBJECT(dlg));
}

static void remove_clicked(GtkWidget* button, gpointer user_data)
{
  GtkTreeModel* model;
  GtkTreeIter iter;

  GtkTreeView* treeview = GTK_TREE_VIEW(user_data);
  if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(treeview), &model, &iter))
    {
      if (question("Do you really want to delete selected item?"))
	{
	  gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
	}
    }
}

static void cell_edited(GtkCellRendererText* renderer,
			gchar* path,
			gchar* new_text,
			gpointer user_data)
{
  GtkListStore* store = GTK_LIST_STORE(user_data);

  GtkTreeIter iter;
  {
    GtkTreePath* tree_path = gtk_tree_path_new_from_string(path);
    gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, tree_path);
    gtk_tree_path_free(tree_path);
  }
  
  gtk_list_store_set(store, &iter,
		     1, new_text,
		     -1);
}

static void page_destroy(GtkWidget* widget, gpointer user_data)
{
  state_t* state = (state_t*)user_data;

  GData* result;
  g_datalist_init(&result);

  GtkTreeIter iter;
  if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(state->store), &iter))
    do
      {
	gchar* key;
	gchar* value;

	gtk_tree_model_get(GTK_TREE_MODEL(state->store), &iter,
			   0, &key,
			   1, &value,
			   -1);

	g_datalist_set_data_full(&result, key, value, g_free);

	g_free(key);
      }
    while (gtk_tree_model_iter_next(GTK_TREE_MODEL(state->store), &iter));

  if (!are_datalists_equal(state->metainfo, result))
    if (question("Do you want to save changes in metainfo?"))
      state->plugin->set_metainfo(state->filename, result);

  /* TODO: free state */
}

static void append_to_list_store(GQuark key_id, gpointer data, gpointer user_data)
{
  GtkListStore* store = GTK_LIST_STORE(user_data);
  GtkTreeIter iter;
  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter,
		     0, g_quark_to_string(key_id),
		     1, (gchar*)data,
		     -1);
}

GtkWidget* get_page(const gchar* filename, const gchar* mime, GError** error)
{
  PluginInterface* plugin = NULL;

  int i;
  for (i = 0; i < PLUGINS_COUNT; ++i)
    if (s_plugins[i]->check_file(filename, mime))
      {
	plugin = s_plugins[i];
	break;
      }

  if (plugin == NULL)
    {
      g_set_error(error,
		  g_quark_from_static_string("core"),
		  1,
		  "Unsupported file type (%s).", mime);
      return NULL;
    }

  GError* metainfo_error = NULL;
  GData* metainfo = plugin->get_metainfo(filename, &metainfo_error);
  if (metainfo_error != NULL)
    {
      g_propagate_error(error, metainfo_error);
      return NULL;
    }

  GtkListStore* store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
  g_datalist_foreach(&metainfo, append_to_list_store, store);

  state_t* state = malloc(sizeof(state_t));
  state->plugin = plugin;
  state->filename = g_strdup(filename);
  state->metainfo = metainfo;
  state->store = store;

  /* UI */

  GtkVBox* vbox = GTK_VBOX(gtk_vbox_new(FALSE, 4));
  gtk_object_set(GTK_OBJECT(vbox),
		 "border-width", 8,
		 NULL);
  g_signal_connect(vbox, "destroy", G_CALLBACK(page_destroy), state);

  GtkScrolledWindow* scrollarea = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
  gtk_scrolled_window_set_shadow_type(scrollarea, GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy(scrollarea, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(scrollarea), TRUE, TRUE, 0);

  GtkTreeView* treeview = GTK_TREE_VIEW(gtk_tree_view_new());
  gtk_container_add(GTK_CONTAINER(scrollarea), GTK_WIDGET(treeview));
    
  gtk_tree_view_set_model(treeview, GTK_TREE_MODEL(store));

  /* columns */
        
  GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes("Key", gtk_cell_renderer_text_new(), "text", 0, NULL);
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_column_set_min_width(column, 100);
  gtk_tree_view_append_column(treeview, column);

  GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
  gtk_object_set(GTK_OBJECT(renderer),
		 "editable", TRUE,
		 "editable-set", TRUE,
		 NULL);
  g_signal_connect(renderer, "edited", G_CALLBACK(cell_edited), store);

  GtkTreeViewColumn* column2 = gtk_tree_view_column_new_with_attributes("Value", renderer, "text", 1, NULL);
  gtk_tree_view_column_set_resizable(column2, TRUE);
  gtk_tree_view_append_column(treeview, column2);

  /* buttons */

  GtkButtonBox* hbuttonbox = GTK_BUTTON_BOX(gtk_hbutton_box_new());
  gtk_button_box_set_layout(hbuttonbox, GTK_BUTTONBOX_END);
  gtk_button_box_set_spacing(hbuttonbox, 4);
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(hbuttonbox), FALSE, TRUE, 0);

  GtkWidget* button_add = gtk_button_new_from_stock(GTK_STOCK_ADD);
  g_signal_connect(button_add, "clicked", G_CALLBACK(add_clicked), store);
  gtk_box_pack_start(GTK_BOX(hbuttonbox), button_add, FALSE, FALSE, 0);

  GtkWidget* button_remove = gtk_button_new_from_stock(GTK_STOCK_REMOVE);
  g_signal_connect(button_remove, "clicked", G_CALLBACK(remove_clicked), treeview);
  gtk_box_pack_start(GTK_BOX(hbuttonbox), button_remove, FALSE, FALSE, 0);

  gtk_widget_show_all(GTK_WIDGET(vbox));
  return GTK_WIDGET(vbox);
}
