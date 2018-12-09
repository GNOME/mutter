#ifndef META_SOUND_H
#define META_SOUND_H

#include <gio/gio.h>

G_DECLARE_FINAL_TYPE (MetaSound, meta_sound, META, SOUND, GObject)

#define META_TYPE_SOUND (meta_sound_get_type ())

void meta_sound_play_from_theme (MetaSound    *sound,
                                 const char   *name,
                                 const char   *description,
                                 GCancellable *cancellable);
void meta_sound_play_from_file  (MetaSound    *sound,
                                 GFile        *file,
                                 const char   *description,
                                 GCancellable *cancellable);

#endif /* META_SOUND_H */
