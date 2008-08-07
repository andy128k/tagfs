#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <libnautilus-extension/nautilus-property-page-provider.h>

#include "nautilus-tageditor.h"

#include "core.h"

static GList* get_pages(NautilusPropertyPageProvider* provider, GList* files)
{
  if (files == NULL || files->next != NULL) // files.length == 0 || files.length > 1
    return NULL;

  NautilusFileInfo* file = files->data; // files[0]

  gboolean file_scheme;
  {
    char* scheme = nautilus_file_info_get_uri_scheme(file);
    file_scheme = !strcmp(scheme, "file");
    g_free(scheme);
  }

  if (!file_scheme)
    return NULL;

  if (nautilus_file_info_is_directory(file))
    return NULL;

  gchar* filename;
  {
    gchar* uri = nautilus_file_info_get_uri(file);
    filename = g_filename_from_uri(uri, NULL, NULL);
    g_free(uri);
  }

  if (filename == NULL)
    return NULL;

  GtkWidget* page = get_page(filename);
  g_free(filename);

  if (page == NULL)
    return NULL;

  gtk_widget_show(page);

  GtkWidget* label = gtk_label_new("Metainfo");
  gtk_widget_show(label);

  NautilusPropertyPage* ppage = nautilus_property_page_new("metainfo", label, page);

  return g_list_prepend(NULL, ppage);
}

static void interface_init(NautilusPropertyPageProviderIface* g_iface, gpointer iface_data)
{
  g_iface->get_pages = get_pages;
}

static GType type_list[1];

static void register_type(GTypeModule* module)
{
  static const GTypeInfo info = {
    sizeof(MetainfoNautilusExtensionClass),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sizeof(MetainfoNautilusExtension),
    0,
    NULL,
  };

  GType extension_type = g_type_module_register_type(module,
						     G_TYPE_OBJECT,
						     "MetainfoNautilusExtension",
						     &info, 0);
  static const GInterfaceInfo property_page_provider_iface_info =
    {
      (GInterfaceInitFunc)interface_init,
      NULL,
      NULL
    };
  g_type_module_add_interface(module, extension_type,
			      NAUTILUS_TYPE_PROPERTY_PAGE_PROVIDER, &property_page_provider_iface_info);

  type_list[0] = extension_type;
}

/* nautilus interface */

void nautilus_module_initialize(GTypeModule* module)
{
  register_type(module);
}

void nautilus_module_list_types(const GType** types, int* num_types)
{
  *types = type_list;
  *num_types = G_N_ELEMENTS(type_list);
}

void nautilus_module_shutdown(void)
{
}
