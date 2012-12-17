/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_BACKGROUND_ACTOR_PRIVATE_H
#define META_BACKGROUND_ACTOR_PRIVATE_H

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-bg.h>

#include <meta/screen.h>
#include <meta/meta-background-actor.h>

void meta_background_actor_set_visible_region  (MetaBackgroundActor *self,
                                                cairo_region_t      *visible_region);

GTask     *meta_background_draw_async          (MetaScreen          *screen,
                                                GnomeBG             *bg,
                                                GCancellable        *cancellable,
                                                GAsyncReadyCallback  callback,
                                                gpointer             user_data);
CoglHandle meta_background_draw_finish         (MetaScreen    *screen,
                                                GAsyncResult  *result,
                                                GError       **error);


#endif /* META_BACKGROUND_ACTOR_PRIVATE_H */
