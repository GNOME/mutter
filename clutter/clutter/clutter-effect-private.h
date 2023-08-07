#pragma once

#include "clutter/clutter-effect.h"

G_BEGIN_DECLS

gboolean        _clutter_effect_modify_paint_volume     (ClutterEffect           *effect,
                                                         ClutterPaintVolume      *volume);
gboolean        _clutter_effect_has_custom_paint_volume (ClutterEffect           *effect);
void            _clutter_effect_paint                   (ClutterEffect           *effect,
                                                         ClutterPaintNode        *node,
                                                         ClutterPaintContext     *paint_context,
                                                         ClutterEffectPaintFlags  flags);
void            _clutter_effect_pick                    (ClutterEffect           *effect,
                                                         ClutterPickContext      *pick_context);

G_END_DECLS
