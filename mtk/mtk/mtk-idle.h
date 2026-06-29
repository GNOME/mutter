#pragma once

#include "mtk/mtk-macros.h"

#include <glib.h>

MTK_EXPORT
guint mtk_idle_add_full (int            priority,
                         GSourceFunc    function,
                         gpointer       data,
                         GDestroyNotify notify);

MTK_EXPORT
guint mtk_idle_add (GSourceFunc function,
                    gpointer    data);

MTK_EXPORT
guint mtk_idle_add_once (GSourceOnceFunc function,
                         gpointer        data);

MTK_EXPORT
guint mtk_timeout_add_full (int            priority,
                            unsigned int   interval,
                            GSourceFunc    function,
                            gpointer       data,
                            GDestroyNotify notify);

MTK_EXPORT
guint mtk_timeout_add (unsigned int interval,
                       GSourceFunc  function,
                       gpointer     data);

MTK_EXPORT
guint mtk_timeout_add_once (unsigned int    interval,
                            GSourceOnceFunc function,
                            gpointer        data);

MTK_EXPORT
guint mtk_timeout_add_seconds_full (int            priority,
                                    unsigned int   interval,
                                    GSourceFunc    function,
                                    gpointer       data,
                                    GDestroyNotify notify);

MTK_EXPORT
guint mtk_timeout_add_seconds (unsigned int interval,
                               GSourceFunc  function,
                               gpointer     data);

MTK_EXPORT
guint mtk_timeout_add_seconds_once (unsigned int    interval,
                                    GSourceOnceFunc function,
                                    gpointer        data);

MTK_EXPORT
gboolean mtk_source_remove (guint tag);

MTK_EXPORT
void mtk_source_set_name_by_id (guint       tag,
                                const char *name);
