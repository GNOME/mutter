#ifndef META_KEYBINDINGS_X11_PRIVATE_H
#define META_KEYBINDINGS_X11_PRIVATE_H

void meta_window_grab_keys (MetaWindow *window);
void meta_window_ungrab_keys (MetaWindow *window);

void meta_x11_display_grab_keys   (MetaX11Display *x11_display);
void meta_x11_display_ungrab_keys (MetaX11Display *x11_display);
void meta_x11_display_ungrab_key_bindings (MetaDisplay *display);
void meta_x11_display_grab_key_bindings (MetaDisplay *display);

void maybe_update_locate_pointer_keygrab (MetaDisplay *display,
                                          gboolean     grab);

void meta_change_keygrab (MetaKeyBindingManager *keys,
                          Window                 xwindow,
                          gboolean               grab,
                          MetaResolvedKeyCombo  *resolved_combo);

#endif
