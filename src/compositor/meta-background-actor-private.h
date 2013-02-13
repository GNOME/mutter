/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef META_BACKGROUND_ACTOR_PRIVATE_H
#define META_BACKGROUND_ACTOR_PRIVATE_H

#include <meta/screen.h>
#include <meta/meta-background-actor.h>

void meta_background_actor_set_visible_region  (MetaBackgroundActor *self,
                                                cairo_region_t      *visible_region);

/**
 * MetaBackgroundSlideshow:
 *
 * A class for handling animated backgrounds.
 */

#define META_TYPE_BACKGROUND_SLIDESHOW            (meta_background_slideshow_get_type ())
#define META_BACKGROUND_SLIDESHOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_BACKGROUND_SLIDESHOW, MetaBackgroundSlideshow))
#define META_BACKGROUND_SLIDESHOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), META_TYPE_BACKGROUND_SLIDESHOW, MetaBackgroundSlideshowClass))
#define META_IS_BACKGROUND_SLIDESHOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_BACKGROUND_SLIDESHOW))
#define META_IS_BACKGROUND_SLIDESHOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), META_TYPE_BACKGROUND_SLIDESHOW))
#define META_BACKGROUND_SLIDESHOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), META_TYPE_BACKGROUND_SLIDESHOW, MetaBackgroundSlideshowClass))

typedef struct _MetaBackgroundSlideshow        MetaBackgroundSlideshow;
typedef struct _MetaBackgroundSlideshowClass   MetaBackgroundSlideshowClass;

GType meta_background_slideshow_get_type (void) G_GNUC_CONST;

MetaBackgroundSlideshow     *meta_background_slideshow_new         (MetaScreen          *screen,
                                                                    const char          *picture_uri);

const char                  *meta_background_slideshow_get_uri     (MetaBackgroundSlideshow *slideshow);

GTask                       *meta_background_slideshow_draw_async  (MetaBackgroundSlideshow  *slideshow,
                                                                    GCancellable             *cancellable,
                                                                    GAsyncReadyCallback       callback,
                                                                    gpointer                  user_data);
CoglHandle                   meta_background_slideshow_draw_finish (MetaBackgroundSlideshow  *slideshow,
                                                                    GAsyncResult             *result,
                                                                    GError                  **error);

int                          meta_background_slideshow_get_next_timeout (MetaBackgroundSlideshow *slideshow);



#endif /* META_BACKGROUND_ACTOR_PRIVATE_H */
