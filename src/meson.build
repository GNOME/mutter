mutter_includesubdir = pkgname / 'meta'
mutter_includedir = includedir / mutter_includesubdir

mutter_includes = [
  include_directories('.'),
  top_includepath,
  clutter_includepath,
  cogl_includepath,
]

mutter_lib_deps = [
  m_dep,
]

mutter_pkg_deps = [
  cairo_dep,
  gio_unix_dep,
  glib_dep,
  gsettings_desktop_schemas_dep,
]

mutter_pkg_private_deps = [
  colord_dep,
  lcms2_dep,
  gmodule_no_export_dep,
  gnome_settings_daemon_dep,
  xkbcommon_dep,
  glycin_dep,
  libeis_dep,
  libdisplay_info_dep,
]

if have_gnome_desktop
  mutter_pkg_private_deps += [
    gnome_desktop_dep,
  ]
endif

if have_sound_player
  mutter_pkg_private_deps += [
    libcanberra_dep,
  ]
endif

if have_gl
  mutter_pkg_deps += [
    gl_dep,
  ]
endif

if have_gles2
  mutter_pkg_private_deps += [
    gles2_dep,
  ]
endif

if have_egl
  mutter_pkg_deps += [
    egl_dep,
  ]
endif

if have_libgudev
  mutter_pkg_private_deps += [
    gudev_dep,
    libudev_dep,
  ]
endif

if have_startup_notification
  mutter_pkg_private_deps += [
    libstartup_notification_dep,
  ]
endif

if have_libwacom
  mutter_pkg_private_deps += [
    libwacom_dep,
  ]
endif

if have_remote_desktop
  mutter_pkg_private_deps += [
    libpipewire_dep,
    libeis_dep,
  ]
endif

if have_introspection
  mutter_pkg_private_deps += [
    gobject_introspection_dep,
  ]
endif

if have_x11_client
  mutter_pkg_deps += [
    xfixes_dep,
    xi_dep,
    x11_dep,
  ]

  mutter_pkg_private_deps += [
    xrandr_dep,
    xinerama_dep,
    xext_dep,
    xcomposite_dep,
    xcursor_dep,
    xdamage_dep,
    x11_xcb_dep,
    xcb_res_dep,
    xau_dep,
  ]
endif

if have_x11
  mutter_pkg_private_deps += [
    fribidi_dep,
    xcb_randr_dep,
    xkbfile_dep,
    xkbcommon_x11_dep,
    xtst_dep,
  ]
endif

if have_wayland
  mutter_pkg_deps += [
    wayland_server_dep,
  ]
endif

if have_logind
  mutter_pkg_private_deps += [
    logind_dep,
  ]
endif

if have_native_backend
  mutter_pkg_private_deps += [
    libinput_dep,
    libgbm_dep,
    xkbcommon_dep,
  ]
endif

if have_wayland or have_native_backend
  mutter_pkg_private_deps += [
    libdrm_dep,
  ]
endif

if have_wayland_eglstream
  mutter_lib_deps += [
    dl_dep,
  ]
  mutter_pkg_private_deps += [
    wayland_eglstream_protocols_dep,
  ]
endif

mutter_deps = [
  mutter_pkg_deps,
  mutter_pkg_private_deps,
  mutter_lib_deps,
]

mutter_c_args = [
  '-DCOGL_ENABLE_MUTTER_API',
  '-DSN_API_NOT_YET_FROZEN=1',
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
]

if get_option('verbose')
  mutter_c_args += [
    '-DWITH_VERBOSE_MODE'
  ]
endif

mutter_sources = [
  'backends/edid.h',
  'backends/edid-parse.c',
  'backends/gsm-inhibitor-flag.h',
  'backends/meta-a11y-manager.c',
  'backends/meta-a11y-manager.h',
  'backends/meta-backend.c',
  'backends/meta-backend-private.h',
  'backends/meta-backlight.c',
  'backends/meta-backlight-private.h',
  'backends/meta-backlight-ref-white.c',
  'backends/meta-backlight-ref-white-private.h',
  'backends/meta-barrier.c',
  'backends/meta-barrier-private.h',
  'backends/meta-color-calibration-session.c',
  'backends/meta-color-calibration-session.h',
  'backends/meta-color-device.c',
  'backends/meta-color-device.h',
  'backends/meta-color-manager.c',
  'backends/meta-color-manager.h',
  'backends/meta-color-manager-private.h',
  'backends/meta-color-profile.c',
  'backends/meta-color-profile.h',
  'backends/meta-color-store.c',
  'backends/meta-color-store.h',
  'backends/meta-connector.c',
  'backends/meta-connector.h',
  'backends/meta-crtc-mode.c',
  'backends/meta-crtc-mode.h',
  'backends/meta-crtc.c',
  'backends/meta-crtc.h',
  'backends/meta-cursor.c',
  'backends/meta-cursor.h',
  'backends/meta-cursor-renderer.c',
  'backends/meta-cursor-renderer.h',
  'backends/meta-cursor-sprite-xcursor.c',
  'backends/meta-cursor-sprite-xcursor.h',
  'backends/meta-cursor-tracker.c',
  'backends/meta-cursor-tracker-private.h',
  'backends/meta-dbus-access-checker.c',
  'backends/meta-dbus-access-checker.h',
  'backends/meta-dbus-session-manager.c',
  'backends/meta-dbus-session-manager.h',
  'backends/meta-dbus-session-watcher.c',
  'backends/meta-dbus-session-watcher.h',
  'backends/meta-dbus-utils.c',
  'backends/meta-dbus-utils.h',
  'backends/meta-display-config-shared.h',
  'backends/meta-dnd-private.h',
  'backends/meta-fd-source.c',
  'backends/meta-fd-source.h',
  'backends/meta-gpu.c',
  'backends/meta-gpu.h',
  'backends/meta-idle-monitor.c',
  'backends/meta-idle-manager.c',
  'backends/meta-idle-manager.h',
  'backends/meta-idle-monitor-private.h',
  'backends/meta-input-capture.c',
  'backends/meta-input-capture.h',
  'backends/meta-input-capture-session.c',
  'backends/meta-input-capture-session.h',
  'backends/meta-input-device.c',
  'backends/meta-input-mapper.c',
  'backends/meta-input-mapper-private.h',
  'backends/meta-input-settings.c',
  'backends/meta-input-settings-private.h',
  'backends/meta-input-settings-dummy.c',
  'backends/meta-input-settings-dummy.h',
  'backends/meta-keymap-utils.c',
  'backends/meta-keymap-utils.h',
  'backends/meta-logical-monitor.c',
  'backends/meta-logical-monitor-private.h',
  'backends/meta-monitor.c',
  'backends/meta-monitor-config-manager.c',
  'backends/meta-monitor-config-manager.h',
  'backends/meta-monitor-config-store.c',
  'backends/meta-monitor-config-store.h',
  'backends/meta-monitor-config-utils.c',
  'backends/meta-monitor-config-utils.h',
  'backends/meta-monitor-private.h',
  'backends/meta-monitor-manager.c',
  'backends/meta-monitor-manager-dummy.c',
  'backends/meta-monitor-manager-dummy.h',
  'backends/meta-monitor-manager-private.h',
  'backends/meta-orientation-manager.c',
  'backends/meta-output.c',
  'backends/meta-output.h',
  'backends/meta-pointer-constraint.c',
  'backends/meta-pointer-constraint.h',
  'backends/meta-remote-access-controller-private.h',
  'backends/meta-remote-access-controller.c',
  'backends/meta-renderdoc.c',
  'backends/meta-renderdoc.h',
  'backends/meta-renderer.c',
  'backends/meta-renderer.h',
  'backends/meta-renderer-view.c',
  'backends/meta-renderer-view.h',
  'backends/meta-screen-cast-window.c',
  'backends/meta-screen-cast-window.h',
  'backends/meta-settings.c',
  'backends/meta-settings-private.h',
  'backends/meta-sprite.c',
  'backends/meta-stage.c',
  'backends/meta-stage-impl.c',
  'backends/meta-stage-impl-private.h',
  'backends/meta-stage-private.h',
  'backends/meta-stage-view.c',
  'backends/meta-stage-view-private.h',
  'backends/meta-viewport-info.c',
  'backends/meta-viewport-info.h',
  'backends/meta-virtual-monitor.c',
  'backends/meta-virtual-monitor.h',
  'compositor/clutter-utils.c',
  'compositor/clutter-utils.h',
  'compositor/cogl-utils.c',
  'compositor/cogl-utils.h',
  'compositor/compositor.c',
  'compositor/compositor-private.h',
  'compositor/edge-resistance.c',
  'compositor/edge-resistance.h',
  'compositor/meta-background-actor.c',
  'compositor/meta-background-content.c',
  'compositor/meta-background-content-private.h',
  'compositor/meta-background.c',
  'compositor/meta-background-group.c',
  'compositor/meta-background-image.c',
  'compositor/meta-background-private.h',
  'compositor/meta-compositor-server.c',
  'compositor/meta-compositor-server.h',
  'compositor/meta-compositor-view.c',
  'compositor/meta-compositor-view.h',
  'compositor/meta-cullable.c',
  'compositor/meta-cullable.h',
  'compositor/meta-dnd-actor.c',
  'compositor/meta-dnd-actor-private.h',
  'compositor/meta-dnd.c',
  'compositor/meta-feedback-actor.c',
  'compositor/meta-feedback-actor-private.h',
  'compositor/meta-later.c',
  'compositor/meta-module.c',
  'compositor/meta-module.h',
  'compositor/meta-multi-texture.c',
  'compositor/meta-multi-texture-format.c',
  'compositor/meta-multi-texture-format-private.h',
  'compositor/meta-plugin.c',
  'compositor/meta-plugin-manager.c',
  'compositor/meta-plugin-manager.h',
  'compositor/meta-shaped-texture.c',
  'compositor/meta-shaped-texture-private.h',
  'compositor/meta-surface-actor.c',
  'compositor/meta-surface-actor.h',
  'compositor/meta-texture-mipmap.c',
  'compositor/meta-texture-mipmap.h',
  'compositor/meta-window-actor.c',
  'compositor/meta-window-actor-private.h',
  'compositor/meta-window-drag.c',
  'compositor/meta-window-drag.h',
  'compositor/meta-window-group.c',
  'compositor/meta-window-group-private.h',
  'core/bell.c',
  'core/bell.h',
  'core/boxes.c',
  'core/boxes-private.h',
  'core/constraints.c',
  'core/constraints.h',
  'core/delete.c',
  'core/display.c',
  'core/display-private.h',
  'core/events.c',
  'core/events.h',
  'core/keybindings.c',
  'core/keybindings-private.h',
  'core/meta-accel-parse.c',
  'core/meta-accel-parse.h',
  'core/meta-border.c',
  'core/meta-border.h',
  'core/meta-clipboard-manager.c',
  'core/meta-clipboard-manager.h',
  'core/meta-close-dialog.c',
  'core/meta-context-main.c',
  'core/meta-context-main.h',
  'core/meta-context-private.h',
  'core/meta-context.c',
  'core/meta-debug-control.c',
  'core/meta-debug-control-private.h',
  'core/meta-fraction.c',
  'core/meta-fraction.h',
  'core/meta-gesture-tracker.c',
  'core/meta-gesture-tracker-private.h',
  'core/meta-inhibit-shortcuts-dialog.c',
  'core/meta-inhibit-shortcuts-dialog-default.c',
  'core/meta-inhibit-shortcuts-dialog-default-private.h',
  'core/meta-launch-context.c',
  'core/meta-pad-action-mapper.c',
  'core/meta-private-enums.h',
  'core/meta-selection.c',
  'core/meta-selection-source.c',
  'core/meta-selection-source-memory.c',
  'core/meta-session-manager.c',
  'core/meta-session-state.c',
  'core/meta-sound-player.c',
  'core/meta-tablet-action-mapper.c',
  'core/meta-tool-action-mapper.c',
  'core/meta-window-config.c',
  'core/meta-window-config-private.h',
  'core/meta-workspace-manager.c',
  'core/meta-workspace-manager-private.h',
  'core/place.c',
  'core/place.h',
  'core/prefs-private.h',
  'core/prefs.c',
  'core/restart.c',
  'core/stack.c',
  'core/stack.h',
  'core/stack-tracker.c',
  'core/stack-tracker.h',
  'core/startup-notification.c',
  'core/startup-notification-private.h',
  'core/util.c',
  'core/util-private.h',
  'core/window.c',
  'core/window-private.h',
  'core/workspace.c',
  'core/workspace-private.h',
]

if have_x11
  mutter_sources += [
    'backends/x11/cm/meta-backend-x11-cm.c',
    'backends/x11/cm/meta-backend-x11-cm.h',
    'backends/x11/cm/meta-cursor-sprite-xfixes.c',
    'backends/x11/cm/meta-cursor-sprite-xfixes.h',
    'backends/x11/cm/meta-renderer-x11-cm.c',
    'backends/x11/cm/meta-renderer-x11-cm.h',
    'backends/x11/meta-backend-x11.c',
    'backends/x11/meta-backend-x11.h',
    'backends/x11/meta-backlight-x11.c',
    'backends/x11/meta-backlight-x11.h',
    'backends/x11/meta-barrier-x11.c',
    'backends/x11/meta-barrier-x11.h',
    'backends/x11/meta-clutter-backend-x11.c',
    'backends/x11/meta-clutter-backend-x11.h',
    'backends/x11/meta-color-manager-x11.c',
    'backends/x11/meta-color-manager-x11.h',
    'backends/x11/meta-crtc-xrandr.c',
    'backends/x11/meta-crtc-xrandr.h',
    'backends/x11/meta-cursor-renderer-x11.c',
    'backends/x11/meta-cursor-renderer-x11.h',
    'backends/x11/meta-cursor-tracker-x11.c',
    'backends/x11/meta-cursor-tracker-x11.h',
    'backends/x11/meta-event-x11.c',
    'backends/x11/meta-event-x11.h',
    'backends/x11/meta-gpu-xrandr.c',
    'backends/x11/meta-gpu-xrandr.h',
    'backends/x11/meta-input-device-x11.c',
    'backends/x11/meta-input-device-x11.h',
    'backends/x11/meta-input-device-tool-x11.c',
    'backends/x11/meta-input-device-tool-x11.h',
    'backends/x11/meta-input-settings-x11.c',
    'backends/x11/meta-input-settings-x11.h',
    'backends/x11/meta-seat-x11.c',
    'backends/x11/meta-seat-x11.h',
    'backends/x11/meta-keymap-x11.c',
    'backends/x11/meta-keymap-x11.h',
    'backends/x11/meta-monitor-manager-xrandr.c',
    'backends/x11/meta-monitor-manager-xrandr.h',
    'backends/x11/meta-output-xrandr.c',
    'backends/x11/meta-output-xrandr.h',
    'backends/x11/meta-renderer-x11.c',
    'backends/x11/meta-renderer-x11.h',
    'backends/x11/meta-sprite-x11.c',
    'backends/x11/meta-sprite-x11.h',
    'backends/x11/meta-stage-x11.c',
    'backends/x11/meta-stage-x11.h',
    'backends/x11/meta-virtual-input-device-x11.c',
    'backends/x11/meta-virtual-input-device-x11.h',
    'backends/x11/meta-xkb-a11y-x11.c',
    'backends/x11/meta-xkb-a11y-x11.h',
    'backends/x11/nested/meta-backend-x11-nested.c',
    'backends/x11/nested/meta-backend-x11-nested.h',
    'backends/x11/nested/meta-cursor-renderer-x11-nested.c',
    'backends/x11/nested/meta-cursor-renderer-x11-nested.h',
    'backends/x11/nested/meta-sprite-x11-nested.c',
    'backends/x11/nested/meta-sprite-x11-nested.h',
    'backends/x11/nested/meta-stage-x11-nested.c',
    'backends/x11/nested/meta-stage-x11-nested.h',
    'backends/x11/nested/meta-renderer-x11-nested.c',
    'backends/x11/nested/meta-renderer-x11-nested.h',
    'compositor/meta-compositor-x11.c',
    'compositor/meta-compositor-x11.h',
    'compositor/meta-surface-actor-x11.c',
    'compositor/meta-surface-actor-x11.h',
    'compositor/meta-sync-ring.c',
    'compositor/meta-sync-ring.h',
  ]
endif

if have_x11_client
  mutter_sources += [
    'compositor/meta-window-actor-x11.c',
    'compositor/meta-window-actor-x11.h',
    'x11/atomnames.h',
    'x11/events.c',
    'x11/events.h',
    'x11/group-props.c',
    'x11/group-props.h',
    'x11/meta-selection-source-x11.c',
    'x11/meta-selection-source-x11-private.h',
    'x11/meta-shadow-factory.c',
    'x11/meta-shadow-factory.h',
    'x11/meta-startup-notification-x11.c',
    'x11/meta-startup-notification-x11.h',
    'x11/meta-sync-counter.c',
    'x11/meta-sync-counter.h',
    'x11/meta-window-shape.c',
    'x11/meta-window-shape.h',
    'x11/meta-x11-display.c',
    'x11/meta-x11-display-private.h',
    'x11/meta-x11-event-source.c',
    'x11/meta-x11-frame.c',
    'x11/meta-x11-frame.h',
    'x11/meta-x11-group.c',
    'x11/meta-x11-group-private.h',
    'x11/meta-x11-selection.c',
    'x11/meta-x11-selection-private.h',
    'x11/meta-x11-selection-input-stream.c',
    'x11/meta-x11-selection-input-stream-private.h',
    'x11/meta-x11-selection-output-stream.c',
    'x11/meta-x11-selection-output-stream-private.h',
    'x11/meta-x11-stack.c',
    'x11/meta-x11-stack-private.h',
    'x11/mutter-Xatomtype.h',
    'x11/window-props.c',
    'x11/window-props.h',
    'x11/window-x11.c',
    'x11/window-x11.h',
    'x11/window-x11-private.h',
    'x11/xprops.c',
    'x11/xprops.h',
  ]
endif


if have_egl_device
  mutter_sources += [
    'backends/native/meta-render-device-egl-stream.c',
    'backends/native/meta-render-device-egl-stream.h',
  ]
endif

if have_egl
  mutter_sources += [
    'backends/meta-egl.c',
    'backends/meta-egl-ext.h',
    'backends/meta-egl.h',
  ]
endif

if have_gles2
  mutter_sources += [
    'backends/meta-gles3.c',
    'backends/meta-gles3.h',
    'backends/meta-gles3-table.h',
  ]
endif

if have_remote_desktop
  mutter_sources += [
    'backends/meta-eis.c',
    'backends/meta-eis.h',
    'backends/meta-eis-client.c',
    'backends/meta-eis-client.h',
    'backends/meta-eis-viewport.c',
    'backends/meta-eis-viewport.h',
    'backends/meta-remote-desktop.c',
    'backends/meta-remote-desktop.h',
    'backends/meta-remote-desktop-session.c',
    'backends/meta-remote-desktop-session.h',
    'backends/meta-screen-cast.c',
    'backends/meta-screen-cast.h',
    'backends/meta-screen-cast-area-stream.c',
    'backends/meta-screen-cast-area-stream.h',
    'backends/meta-screen-cast-area-stream-src.c',
    'backends/meta-screen-cast-area-stream-src.h',
    'backends/meta-screen-cast-monitor-stream.c',
    'backends/meta-screen-cast-monitor-stream.h',
    'backends/meta-screen-cast-monitor-stream-src.c',
    'backends/meta-screen-cast-monitor-stream-src.h',
    'backends/meta-screen-cast-virtual-stream-src.c',
    'backends/meta-screen-cast-virtual-stream-src.h',
    'backends/meta-screen-cast-virtual-stream.c',
    'backends/meta-screen-cast-virtual-stream.h',
    'backends/meta-screen-cast-window-stream-src.c',
    'backends/meta-screen-cast-window-stream-src.h',
    'backends/meta-screen-cast-window-stream.c',
    'backends/meta-screen-cast-window-stream.h',
    'backends/meta-screen-cast-session.c',
    'backends/meta-screen-cast-session.h',
    'backends/meta-screen-cast-stream.c',
    'backends/meta-screen-cast-stream.h',
    'backends/meta-screen-cast-stream-src.c',
    'backends/meta-screen-cast-stream-src.h',
    'core/meta-selection-source-remote.c',
    'core/meta-selection-source-remote.h',
  ]
endif

if have_wayland
  mutter_sources += [
    'compositor/meta-surface-actor-wayland.c',
    'compositor/meta-surface-actor-wayland.h',
    'compositor/meta-window-actor-wayland.c',
    'compositor/meta-window-actor-wayland.h',
    'core/meta-service-channel.c',
    'core/meta-service-channel.h',
    'wayland/meta-cursor-sprite-wayland.c',
    'wayland/meta-cursor-sprite-wayland.h',
    'wayland/meta-pointer-confinement-wayland.c',
    'wayland/meta-pointer-confinement-wayland.h',
    'wayland/meta-pointer-lock-wayland.c',
    'wayland/meta-pointer-lock-wayland.h',
    'wayland/meta-selection-source-wayland.c',
    'wayland/meta-selection-source-wayland-private.h',
    'wayland/meta-wayland-activation.c',
    'wayland/meta-wayland-activation.h',
    'wayland/meta-wayland-actor-surface.c',
    'wayland/meta-wayland-actor-surface.h',
    'wayland/meta-wayland-buffer.c',
    'wayland/meta-wayland-buffer.h',
    'wayland/meta-wayland.c',
    'wayland/meta-wayland-client.c',
    'wayland/meta-wayland-client-private.h',
    'wayland/meta-wayland-color-management.c',
    'wayland/meta-wayland-color-management.h',
    'wayland/meta-wayland-color-representation.c',
    'wayland/meta-wayland-color-representation.h',
    'wayland/meta-wayland-commit-timing.c',
    'wayland/meta-wayland-cursor-shape.c',
    'wayland/meta-wayland-cursor-shape.h',
    'wayland/meta-wayland-fifo.c',
    'wayland/meta-wayland-cursor-surface.c',
    'wayland/meta-wayland-cursor-surface.h',
    'wayland/meta-wayland-data-device.c',
    'wayland/meta-wayland-data-device.h',
    'wayland/meta-wayland-data-device-primary.c',
    'wayland/meta-wayland-data-device-primary.h',
    'wayland/meta-wayland-data-offer.c',
    'wayland/meta-wayland-data-offer.h',
    'wayland/meta-wayland-data-offer-primary.c',
    'wayland/meta-wayland-data-offer-primary.h',
    'wayland/meta-wayland-data-source.c',
    'wayland/meta-wayland-data-source.h',
    'wayland/meta-wayland-data-source-primary.c',
    'wayland/meta-wayland-data-source-primary.h',
    'wayland/meta-wayland-dma-buf.c',
    'wayland/meta-wayland-dma-buf.h',
    'wayland/meta-wayland-dnd-surface.c',
    'wayland/meta-wayland-dnd-surface.h',
    'wayland/meta-wayland-filter-manager.c',
    'wayland/meta-wayland-filter-manager.h',
    'wayland/meta-wayland-fixes.c',
    'wayland/meta-wayland-fixes.h',
    'wayland/meta-wayland-fractional-scale.c',
    'wayland/meta-wayland-fractional-scale.h',
    'wayland/meta-wayland-gtk-shell.c',
    'wayland/meta-wayland-gtk-shell.h',
    'wayland/meta-wayland.h',
    'wayland/meta-wayland-idle-inhibit.c',
    'wayland/meta-wayland-idle-inhibit.h',
    'wayland/meta-wayland-inhibit-shortcuts.c',
    'wayland/meta-wayland-inhibit-shortcuts-dialog.c',
    'wayland/meta-wayland-inhibit-shortcuts-dialog.h',
    'wayland/meta-wayland-inhibit-shortcuts.h',
    'wayland/meta-wayland-input.c',
    'wayland/meta-wayland-input.h',
    'wayland/meta-wayland-input-device.c',
    'wayland/meta-wayland-input-device.h',
    'wayland/meta-wayland-keyboard.c',
    'wayland/meta-wayland-keyboard.h',
    'wayland/meta-wayland-legacy-xdg-foreign.c',
    'wayland/meta-wayland-legacy-xdg-foreign.h',
    'wayland/meta-wayland-linux-drm-syncobj.c',
    'wayland/meta-wayland-linux-drm-syncobj.h',
    'wayland/meta-wayland-outputs.c',
    'wayland/meta-wayland-outputs.h',
    'wayland/meta-wayland-pointer.c',
    'wayland/meta-wayland-pointer-constraints.c',
    'wayland/meta-wayland-pointer-constraints.h',
    'wayland/meta-wayland-pointer-gesture-hold.c',
    'wayland/meta-wayland-pointer-gesture-hold.h',
    'wayland/meta-wayland-pointer-gesture-pinch.c',
    'wayland/meta-wayland-pointer-gesture-pinch.h',
    'wayland/meta-wayland-pointer-gestures.c',
    'wayland/meta-wayland-pointer-gestures.h',
    'wayland/meta-wayland-pointer-gesture-swipe.c',
    'wayland/meta-wayland-pointer-gesture-swipe.h',
    'wayland/meta-wayland-pointer-warp.c',
    'wayland/meta-wayland-pointer-warp.h',
    'wayland/meta-wayland-pointer.h',
    'wayland/meta-wayland-popup.c',
    'wayland/meta-wayland-popup.h',
    'wayland/meta-wayland-presentation-time.c',
    'wayland/meta-wayland-presentation-time-private.h',
    'wayland/meta-wayland-private.h',
    'wayland/meta-wayland-region.c',
    'wayland/meta-wayland-region.h',
    'wayland/meta-wayland-seat.c',
    'wayland/meta-wayland-seat.h',
    'wayland/meta-wayland-shell-surface.c',
    'wayland/meta-wayland-shell-surface.h',
    'wayland/meta-wayland-single-pixel-buffer.c',
    'wayland/meta-wayland-single-pixel-buffer.h',
    'wayland/meta-wayland-subsurface.c',
    'wayland/meta-wayland-subsurface.h',
    'wayland/meta-wayland-surface.c',
    'wayland/meta-wayland-surface-private.h',
    'wayland/meta-wayland-system-bell.c',
    'wayland/meta-wayland-system-bell.h',
    'wayland/meta-wayland-tablet.c',
    'wayland/meta-wayland-tablet-cursor-surface.c',
    'wayland/meta-wayland-tablet-cursor-surface.h',
    'wayland/meta-wayland-tablet.h',
    'wayland/meta-wayland-tablet-manager.c',
    'wayland/meta-wayland-tablet-manager.h',
    'wayland/meta-wayland-tablet-pad.c',
    'wayland/meta-wayland-tablet-pad-group.c',
    'wayland/meta-wayland-tablet-pad-group.h',
    'wayland/meta-wayland-tablet-pad.h',
    'wayland/meta-wayland-tablet-pad-dial.c',
    'wayland/meta-wayland-tablet-pad-dial.h',
    'wayland/meta-wayland-tablet-pad-ring.c',
    'wayland/meta-wayland-tablet-pad-ring.h',
    'wayland/meta-wayland-tablet-pad-strip.c',
    'wayland/meta-wayland-tablet-pad-strip.h',
    'wayland/meta-wayland-tablet-seat.c',
    'wayland/meta-wayland-tablet-seat.h',
    'wayland/meta-wayland-tablet-tool.c',
    'wayland/meta-wayland-tablet-tool.h',
    'wayland/meta-wayland-text-input.c',
    'wayland/meta-wayland-text-input.h',
    'wayland/meta-wayland-toplevel-drag.c',
    'wayland/meta-wayland-toplevel-drag.h',
    'wayland/meta-wayland-touch.c',
    'wayland/meta-wayland-touch.h',
    'wayland/meta-wayland-transaction.c',
    'wayland/meta-wayland-transaction.h',
    'wayland/meta-wayland-types.h',
    'wayland/meta-wayland-versions.h',
    'wayland/meta-wayland-viewporter.c',
    'wayland/meta-wayland-viewporter.h',
    'wayland/meta-wayland-window-configuration.c',
    'wayland/meta-wayland-window-configuration.h',
    'wayland/meta-wayland-xdg-foreign.c',
    'wayland/meta-wayland-xdg-foreign.h',
    'wayland/meta-wayland-xdg-foreign-private.h',
    'wayland/meta-wayland-xdg-session-manager.c',
    'wayland/meta-wayland-xdg-session-manager.h',
    'wayland/meta-wayland-xdg-session-state.c',
    'wayland/meta-wayland-xdg-session-state.h',
    'wayland/meta-wayland-xdg-session.c',
    'wayland/meta-wayland-xdg-session.h',
    'wayland/meta-wayland-xdg-shell.c',
    'wayland/meta-wayland-xdg-shell.h',
    'wayland/meta-wayland-xdg-dialog.c',
    'wayland/meta-wayland-xdg-dialog.h',
    'wayland/meta-wayland-xdg-toplevel-tag.c',
    'wayland/meta-wayland-xdg-toplevel-tag.h',
    'wayland/meta-window-wayland.c',
    'wayland/meta-window-wayland.h',
  ]

  if have_xwayland
    mutter_sources += [
      'wayland/meta-wayland-x11-interop.c',
      'wayland/meta-wayland-x11-interop.h',
      'wayland/meta-window-xwayland.c',
      'wayland/meta-window-xwayland.h',
      'wayland/meta-xwayland.c',
      'wayland/meta-xwayland-grab-keyboard.c',
      'wayland/meta-xwayland-grab-keyboard.h',
      'wayland/meta-xwayland.h',
      'wayland/meta-xwayland-private.h',
      'wayland/meta-xwayland-dnd.c',
      'wayland/meta-xwayland-dnd-private.h',
      'wayland/meta-xwayland-surface.c',
      'wayland/meta-xwayland-surface.h',
    ]
  endif

  if have_native_backend
    mutter_sources += [
      'wayland/meta-wayland-drm-lease.c',
      'wayland/meta-wayland-drm-lease.h',
    ]
  endif
endif

if have_native_backend
  mutter_sources += [
    'backends/native/meta-backend-native.c',
    'backends/native/meta-backend-native.h',
    'backends/native/meta-backend-native-private.h',
    'backends/native/meta-backend-native-types.h',
    'backends/native/meta-barrier-native.c',
    'backends/native/meta-barrier-native.h',
    'backends/native/meta-bezier.c',
    'backends/native/meta-bezier.h',
    'backends/native/meta-clutter-backend-native.c',
    'backends/native/meta-clutter-backend-native.h',
    'backends/native/meta-crtc-kms.c',
    'backends/native/meta-crtc-kms.h',
    'backends/native/meta-crtc-native.c',
    'backends/native/meta-crtc-native.h',
    'backends/native/meta-crtc-mode-kms.c',
    'backends/native/meta-crtc-mode-kms.h',
    'backends/native/meta-crtc-mode-virtual.c',
    'backends/native/meta-crtc-mode-virtual.h',
    'backends/native/meta-crtc-virtual.c',
    'backends/native/meta-crtc-virtual.h',
    'backends/native/meta-cursor-renderer-native.c',
    'backends/native/meta-cursor-renderer-native.h',
    'backends/native/meta-device-pool-private.h',
    'backends/native/meta-device-pool.c',
    'backends/native/meta-device-pool.h',
    'backends/native/meta-drm-buffer-dumb.c',
    'backends/native/meta-drm-buffer-dumb.h',
    'backends/native/meta-drm-buffer-gbm.c',
    'backends/native/meta-drm-buffer-gbm.h',
    'backends/native/meta-drm-buffer-import.c',
    'backends/native/meta-drm-buffer-import.h',
    'backends/native/meta-drm-buffer-private.h',
    'backends/native/meta-drm-buffer.c',
    'backends/native/meta-drm-buffer.h',
    'backends/native/meta-egl-gbm.c',
    'backends/native/meta-egl-gbm.h',
    'backends/native/meta-gpu-kms.c',
    'backends/native/meta-gpu-kms.h',
    'backends/native/meta-frame-native.c',
    'backends/native/meta-frame-native.h',
    'backends/native/meta-input-device-native.c',
    'backends/native/meta-input-device-native.h',
    'backends/native/meta-input-device-tool-native.c',
    'backends/native/meta-input-device-tool-native.h',
    'backends/native/meta-input-settings-native.c',
    'backends/native/meta-input-settings-native.h',
    'backends/native/meta-keymap-native-private.h',
    'backends/native/meta-keymap-native.c',
    'backends/native/meta-keymap-native.h',
    'backends/native/meta-monitor-manager-native.c',
    'backends/native/meta-monitor-manager-native.h',
    'backends/native/meta-output-kms.c',
    'backends/native/meta-output-kms.h',
    'backends/native/meta-output-native.c',
    'backends/native/meta-output-native.h',
    'backends/native/meta-output-virtual.c',
    'backends/native/meta-output-virtual.h',
    'backends/native/meta-kms-connector-private.h',
    'backends/native/meta-kms-connector.c',
    'backends/native/meta-kms-connector.h',
    'backends/native/meta-kms-crtc-private.h',
    'backends/native/meta-kms-crtc.c',
    'backends/native/meta-kms-crtc.h',
    'backends/native/meta-kms-cursor-manager.c',
    'backends/native/meta-kms-cursor-manager.h',
    'backends/native/meta-kms-device-private.h',
    'backends/native/meta-kms-device.c',
    'backends/native/meta-kms-device.h',
    'backends/native/meta-kms-impl-device-atomic.c',
    'backends/native/meta-kms-impl-device-atomic.h',
    'backends/native/meta-kms-impl-device-dummy.c',
    'backends/native/meta-kms-impl-device-dummy.h',
    'backends/native/meta-kms-impl-device-simple.c',
    'backends/native/meta-kms-impl-device-simple.h',
    'backends/native/meta-kms-impl-device.c',
    'backends/native/meta-kms-impl-device.h',
    'backends/native/meta-kms-impl.c',
    'backends/native/meta-kms-impl.h',
    'backends/native/meta-drm-lease.c',
    'backends/native/meta-drm-lease.h',
    'backends/native/meta-kms-mode.c',
    'backends/native/meta-kms-mode.h',
    'backends/native/meta-kms-page-flip.c',
    'backends/native/meta-kms-page-flip-private.h',
    'backends/native/meta-kms-plane.c',
    'backends/native/meta-kms-plane-private.h',
    'backends/native/meta-kms-plane.h',
    'backends/native/meta-kms-private.h',
    'backends/native/meta-kms-types-private.h',
    'backends/native/meta-kms-types.h',
    'backends/native/meta-kms-update-private.h',
    'backends/native/meta-kms-update.c',
    'backends/native/meta-kms-update.h',
    'backends/native/meta-kms-utils.c',
    'backends/native/meta-kms-utils.h',
    'backends/native/meta-kms.c',
    'backends/native/meta-kms.h',
    'backends/native/meta-onscreen-native.c',
    'backends/native/meta-onscreen-native.h',
    'backends/native/meta-pointer-constraint-native.c',
    'backends/native/meta-pointer-constraint-native.h',
    'backends/native/meta-render-device-gbm.c',
    'backends/native/meta-render-device-gbm.h',
    'backends/native/meta-render-device-private.h',
    'backends/native/meta-render-device-surfaceless.c',
    'backends/native/meta-render-device-surfaceless.h',
    'backends/native/meta-render-device.c',
    'backends/native/meta-render-device.h',
    'backends/native/meta-renderer-native-gles3.c',
    'backends/native/meta-renderer-native-gles3.h',
    'backends/native/meta-renderer-native-private.h',
    'backends/native/meta-renderer-native.c',
    'backends/native/meta-renderer-native.h',
    'backends/native/meta-renderer-view-native.c',
    'backends/native/meta-renderer-view-native.h',
    'backends/native/meta-seat-impl.c',
    'backends/native/meta-seat-impl.h',
    'backends/native/meta-seat-native.c',
    'backends/native/meta-seat-native.h',
    'backends/native/meta-sprite-native.c',
    'backends/native/meta-sprite-native.h',
    'backends/native/meta-stage-native.c',
    'backends/native/meta-stage-native.h',
    'backends/native/meta-thread-impl.c',
    'backends/native/meta-thread-impl.h',
    'backends/native/meta-thread-private.h',
    'backends/native/meta-thread.c',
    'backends/native/meta-thread.h',
    'backends/native/meta-thread-private.h',
    'backends/native/meta-virtual-input-device-native.c',
    'backends/native/meta-virtual-input-device-native.h',
    'backends/native/meta-virtual-monitor-native.c',
    'backends/native/meta-virtual-monitor-native.h',
    'backends/native/meta-xkb-utils.c',
    'backends/native/meta-xkb-utils.h',
    'compositor/meta-compositor-native.c',
    'compositor/meta-compositor-native.h',
    'compositor/meta-compositor-view-native.c',
    'compositor/meta-compositor-view-native.h',
  ]
endif

if have_devkit
  mutter_sources += [
    'core/meta-mdk.c',
    'core/meta-mdk.h',
  ]
endif

if have_wayland or have_native_backend
  mutter_sources += [
    'common/meta-cogl-drm-formats.c',
    'common/meta-cogl-drm-formats.h',
    'common/meta-drm-format-helpers.c',
    'common/meta-drm-format-helpers.h',
    'common/meta-drm-timeline.c',
    'common/meta-drm-timeline.h',
  ]
endif

if have_wayland_eglstream
  mutter_sources += [
    'wayland/meta-wayland-egl-stream.c',
    'wayland/meta-wayland-egl-stream.h',
  ]
endif

if have_libgudev
  mutter_sources += [
    'backends/meta-backlight-sysfs.c',
    'backends/meta-backlight-sysfs-private.h',
    'backends/meta-udev.c',
    'backends/meta-udev.h',
  ]
endif

if have_logind
  mutter_sources += [
    'backends/meta-launcher.c',
    'backends/meta-launcher.h',
  ]
endif

mutter_private_enum_sources = [
  'core/window-private.h',
]

if have_remote_desktop
  mutter_private_enum_sources += [
    'backends/meta-screen-cast.h',
    'backends/meta-screen-cast-session.h',
  ]
endif

if have_native_backend
  mutter_private_enum_sources += [
    'backends/native/meta-backend-native-types.h',
    'backends/native/meta-drm-buffer.h',
    'backends/native/meta-kms-types.h',
    'backends/native/meta-thread.h',
  ]
endif

mutter_built_sources = []

if mutter_private_enum_sources.length() > 0
  mutter_private_enum_types = gnome.mkenums('meta-private-enum-types',
    sources: mutter_private_enum_sources,
    c_template: 'meta-private-enum-types.c.in',
    h_template: 'meta-private-enum-types.h.in',
  )

  mutter_built_sources += mutter_private_enum_types
endif

dbus_interfaces = [
  {
    'name': 'meta-dbus-a11y',
    'interface': 'org.freedesktop.a11y.xml',
    'prefix': 'org.freedesktop.a11y',
  },
  {
    'name': 'meta-dbus-display-config',
    'interface': 'org.gnome.Mutter.DisplayConfig.xml',
    'prefix': 'org.gnome.Mutter.',
  },
  {
    'name': 'meta-dbus-idle-monitor',
    'interface': 'org.gnome.Mutter.IdleMonitor.xml',
    'prefix': 'org.gnome.Mutter.',
    'object_manager': true,
  },
  {
    'name': 'meta-dbus-input-capture',
    'interface': 'org.gnome.Mutter.InputCapture.xml',
    'prefix': 'org.gnome.Mutter',
  },
  {
    'name': 'meta-dbus-input-mapping',
    'interface': 'org.gnome.Mutter.InputMapping.xml',
    'prefix': 'org.gnome.Mutter.',
  },
  {
    'name': 'meta-dbus-service-channel',
    'interface': 'org.gnome.Mutter.ServiceChannel.xml',
    'prefix': 'org.gnome.Mutter.',
  },
  {
    'name': 'meta-dbus-debug-control',
    'interface': 'org.gnome.Mutter.DebugControl.xml',
    'prefix': 'org.gnome.Mutter',
  },
  {
    'name': 'meta-dbus-x11',
    'interface': 'org.gnome.Mutter.X11.xml',
    'prefix': 'org.gnome.Mutter',
  },
  {
    'name': 'meta-dbus-color-manager',
    'interface': 'org.gnome.Mutter.ColorManager.xml',
    'prefix': 'org.gnome.Mutter',
  },
  {
    'name': 'meta-dbus-login1',
    'interface': 'org.freedesktop.login1.xml',
    'prefix': 'org.freedesktop.',
  },
]

if have_profiler
  mutter_sources += [
    'core/meta-profiler.c',
    'core/meta-profiler.h',
  ]

  dbus_interfaces += [
    {
      'name': 'meta-dbus-sysprof3-profiler',
      'interface': 'org.gnome.Sysprof3.Profiler.xml',
      'prefix': 'org.gnome.',
      'directory': sysprof_dbus_interfaces_dir,
    },
  ]
endif

if have_native_backend
  cvt = find_program('cvt')

  gen_default_modes = find_program('backends/native/gen-default-modes.py')
  default_modes_h = custom_target('meta-default-modes',
    output: 'meta-default-modes.h',
    command: [gen_default_modes, '@OUTPUT@']
  )
  mutter_built_sources += default_modes_h
endif

if have_remote_desktop
  dbus_interfaces += [
    {
      'name': 'meta-dbus-remote-desktop',
      'interface': 'org.gnome.Mutter.RemoteDesktop.xml',
      'prefix': 'org.gnome.Mutter.',
    },
    {
      'name': 'meta-dbus-screen-cast',
      'interface': 'org.gnome.Mutter.ScreenCast.xml',
      'prefix': 'org.gnome.Mutter.',
    },
  ]
endif

if have_devkit
  dbus_interfaces += [
    {
      'name': 'meta-dbus-devkit',
      'interface': 'org.gnome.Mutter.Devkit.xml',
      'prefix': 'org.gnome.Mutter.',
    },
  ]
endif

dbus_interfaces += [
  {
    'name': 'meta-dbus-rtkit1',
    'interface': 'org.freedesktop.RealtimeKit1.xml',
    'prefix': 'org.freedesktop.',
  },
  {
    'name': 'meta-dbus-gsd-color',
    'interface': 'org.gnome.SettingsDaemon.Color.xml',
    'prefix': 'org.gnome.',
  },
  {
    'name': 'meta-dbus-gsd-power-screen',
    'interface': 'org.gnome.SettingsDaemon.Power.Screen.xml',
    'prefix': 'org.gnome.',
  },
]

built_dbus_sources = {}

foreach dbus_interface: dbus_interfaces
  name = dbus_interface['name']

  interface = dbus_interface['interface']
  if dbus_interface.has_key('directory')
    directory = dbus_interface['directory']
  else
    directory = dbus_interfaces_dir
  endif
  interface_file = directory / interface

  interface_prefix = dbus_interface['prefix']

  if dbus_interface.has_key('object_manager')
    object_manager = dbus_interface['object_manager']
  else
    object_manager = false
  endif

  dbus_sources = gnome.gdbus_codegen(name, interface_file,
    interface_prefix: interface_prefix,
    namespace: 'MetaDBus',
    object_manager: object_manager,
  )
  built_dbus_sources += {
    name: dbus_sources,
  }
  mutter_built_sources += dbus_sources
endforeach

wayland_protocol_server_headers = []
wayland_protocol_client_headers = []
wayland_protocol_sources = []

if have_wayland
  # Format:
  #  - protocol name
  #  - protocol type ('stable', 'staging', 'unstable', 'private', 'eglstreams')
  #    * 'stable', 'staging' and 'unstable' are from wayland-protocols
  #    * 'private' are shipped in this repo
  #    * 'eglstreams' are from wayland-eglstream-protocols
  #  - protocol version (optional for 'stable', 'staging' and 'unstable')
  wayland_protocols = [
    ['color-management', 'staging', 1, ],
    ['color-representation', 'staging', 1, ],
    ['commit-timing', 'staging', 1, ],
    ['cursor-shape', 'staging', 1, ],
    ['drm-lease', 'staging', 1, ],
    ['fractional-scale', 'staging', 1, ],
    ['fifo', 'staging', 1, ],
    ['gtk-shell', 'private', ],
    ['idle-inhibit', 'unstable', 1, ],
    ['keyboard-shortcuts-inhibit', 'unstable', 1, ],
    ['linux-dmabuf', 'stable', 1, ],
    ['linux-drm-syncobj', 'staging', 1, ],
    ['pointer-constraints', 'unstable', 1, ],
    ['pointer-gestures', 'unstable', 1, ],
    ['pointer-warp', 'staging', 1, ],
    ['presentation-time', 'stable', ],
    ['primary-selection', 'unstable', 1, ],
    ['relative-pointer', 'unstable', 1, ],
    ['session-management-v1', 'private', ],
    ['single-pixel-buffer', 'staging', 1, ],
    ['tablet', 'stable', 2, ],
    ['text-input', 'unstable', 3, ],
    ['viewporter', 'stable', ],
    ['xdg-activation', 'staging', 1, ],
    ['xdg-dialog', 'staging', 1, ],
    ['xdg-foreign', 'unstable', 1, ],
    ['xdg-foreign', 'unstable', 2, ],
    ['xdg-output', 'unstable', 1, ],
    ['xdg-shell', 'stable', ],
    ['xdg-system-bell', 'staging', 1, ],
    ['xdg-toplevel-drag', 'staging', 1, ],
    ['xdg-toplevel-tag', 'staging', 1, ],
    ['xwayland-keyboard-grab', 'unstable', 1, ],
  ]
  if have_xwayland
    wayland_protocols += [
      ['mutter-x11-interop', 'private', ],
    ]
  endif
  if have_wayland_eglstream
    wayland_protocols += [
      ['wayland-eglstream-controller', 'eglstreams', ],
    ]
    wayland_eglstream_protocols_dir = wayland_eglstream_protocols_dep.get_variable('pkgdatadir')
  endif

  protocol_xmls = files()

  foreach p: wayland_protocols
    protocol_name = p.get(0)
    protocol_type = p.get(1)

    if protocol_type == 'private'
      protocol_xmls += files(
        'wayland/protocol/@0@.xml'.format(protocol_name)
      )
    elif protocol_type == 'eglstreams'
      protocol_xmls += files(
        '@0@/@1@.xml'.format(wayland_eglstream_protocols_dir, protocol_name)
      )
    else
      if p.length() > 2
        protocol_xmls += wl_mod.find_protocol(
          protocol_name,
          state: protocol_type,
          version: p.get(2))
      else
        protocol_xmls += wl_mod.find_protocol(
          protocol_name,
          state: protocol_type)
      endif
    endif
  endforeach

  foreach xml: protocol_xmls
    generated = wl_mod.scan_xml(xml,
      client: true,
      server: true)

    wayland_protocol_sources += generated[0]
    wayland_protocol_client_headers += generated[1]
    wayland_protocol_server_headers += generated[2]
  endforeach

  message('Generated code for @0@ wayland protocols'.format(wayland_protocol_sources.length()))
endif

mutter_built_sources += wayland_protocol_server_headers
mutter_built_sources += wayland_protocol_sources

subdir('meta')

mutter_built_sources += mutter_enum_types

mutter_built_headers = []
foreach built_source : mutter_built_sources
  if built_source.full_path().endswith('.h')
    mutter_built_headers += [built_source]
  endif
endforeach

subdir('helpers')

subdir('third_party')

libmutter = library(libmutter_name,
  mutter_sources,
  mutter_built_sources,
  version: '0.0.0',
  soversion: 0,
  gnu_symbol_visibility: 'hidden',
  include_directories: mutter_includes,
  c_args: [
    mutter_c_args,
    '-DG_LOG_DOMAIN="libmutter"',
  ],
  dependencies: [
    libmutter_cogl_dep,
    libmutter_clutter_dep,
    mutter_deps,
    gvdb_dep,
  ],
  install_rpath: pkglibdir,
  install_dir: libdir,
  install: true,
)

libmutter_dep = declare_dependency(
  link_with: libmutter,
  include_directories: mutter_includes,
  sources: mutter_built_headers,
  dependencies: [
    libmutter_cogl_dep,
    libmutter_clutter_dep,
    mutter_deps,
  ],
)

mutter = executable('mutter',
  sources: [
    files('core/mutter.c'),
  ],
  include_directories: mutter_includes,
  c_args: [
    mutter_c_args,
    '-DG_LOG_DOMAIN="mutter"',
  ],
  dependencies: [libmutter_dep],
  install_dir: bindir,
  install: true,
)

if have_introspection
  mutter_introspected_sources = []
  foreach source : mutter_sources
    if source.endswith('.c')
      mutter_introspected_sources += source
    endif
  endforeach

  libmutter_gir_includes = [
    'Gio-2.0',
    'GObject-2.0',
    'GDesktopEnums-3.0',
    libmutter_cogl_gir[0],
    libmutter_clutter_gir[0],
    libmutter_mtk_gir[0],
  ]
  if have_x11_client
    libmutter_gir_includes += [
      'xlib-2.0',
      'xfixes-4.0',
    ]
  endif
  libmutter_gir = gnome.generate_gir(libmutter,
    sources: [
      mutter_enum_types[1],
      mutter_introspected_sources,
      mutter_public_header_files
    ],
    nsversion: libmutter_api_version,
    namespace: 'Meta',
    symbol_prefix: 'meta',
    includes: libmutter_gir_includes,
    dependencies: [
      libmutter_dep,
      mutter_deps,
    ],
    export_packages: ['libmutter-@0@'.format(libmutter_api_version)],
    extra_args: mutter_c_args + introspection_args,
    kwargs: introspection_common,
  )
endif

pkg.generate(libmutter,
  name: 'Meta',
  filebase: 'libmutter-' + libmutter_api_version,
  description: 'Mutter compositor and window manager library',
  subdirs: pkgname,
  requires: [mutter_pkg_deps, libmutter_clutter_name],
  version: meson.project_version(),
  variables: [
    'apiversion=' + libmutter_api_version,
    'girdir=${libdir}/mutter-' + libmutter_api_version,
    'typelibdir=${libdir}/mutter-' + libmutter_api_version,
    'have_x11=@0@'.format(have_x11),
    'have_x11_client=@0@'.format(have_x11_client),
    'have_wayland=@0@'.format(have_wayland),
    'have_fonts=@0@'.format(have_fonts),
  ],
  install_dir: pcdir,
)

subdir('compositor/plugins')
if have_x11_client
  subdir('frames')
endif

if have_tests
  subdir('tests')
endif
