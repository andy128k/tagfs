#ifndef PROPERTY_PAGE_H
#define PROPERTY_PAGE_H

#include <glib-object.h>
/*
 * Type macros.
 */

typedef struct _MetainfoNautilusExtension MetainfoNautilusExtension;
typedef struct _MetainfoNautilusExtensionClass MetainfoNautilusExtensionClass;

struct _MetainfoNautilusExtension
{
  GObject parent;
};

struct _MetainfoNautilusExtensionClass {
  GObjectClass parent;
};

/* used by MAMAN_TYPE_BAR */
GType metainfo_nautilus_extension_get_type(void);

/*
 * Method definitions.
 */

#endif
