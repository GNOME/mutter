cogl_cogl_includesubdir = cogl_includesubdir / 'cogl'
cogl_cogl_includedir = cogl_includedir / 'cogl'

cogl_gir_includes = [
  'GL-1.0',
  'GObject-2.0',
  'Graphene-1.0',
]

if have_introspection
  cogl_gir_includes += [
    libmutter_mtk_gir[0],
  ]
endif

cogl_deprecated_headers = [
  'deprecated/cogl-program.h',
  'deprecated/cogl-shader.h',
  'deprecated/cogl-clutter.h',
]

cogl_headers = [
  'cogl.h',
  'cogl-atlas-texture.h',
  'cogl-attribute-buffer.h',
  'cogl-attribute.h',
  'cogl-bitmap.h',
  'cogl-color.h',
  'cogl1-context.h',
  'cogl-context.h',
  'cogl-depth-state.h',
  'cogl-display.h',
  'cogl-dma-buf-handle.h',
  'cogl-fence.h',
  'cogl-framebuffer.h',
  'cogl-frame-info.h',
  'cogl-glib-source.h',
  'cogl-graphene.h',
  'cogl-index-buffer.h',
  'cogl-indices.h',
  'cogl-macros.h',
  'cogl-matrix-stack.h',
  'cogl-meta-texture.h',
  'cogl-offscreen.h',
  'cogl-onscreen.h',
  'cogl-onscreen-template.h',
  'cogl-output.h',
  'cogl-pipeline.h',
  'cogl-pipeline-layer-state.h',
  'cogl-pipeline-state.h',
  'cogl-pixel-buffer.h',
  'cogl-poll.h',
  'cogl-primitive.h',
  'cogl-primitive-texture.h',
  'cogl-renderer.h',
  'cogl-scanout.h',
  'cogl-snippet.h',
  'cogl-sub-texture.h',
  'cogl-swap-chain.h',
  'cogl-texture-2d.h',
  'cogl-texture-2d-sliced.h',
  'cogl-texture.h',
  'cogl-trace.h',
  'cogl-types.h',
]

cogl_nonintrospected_headers = [
]

cogl_nodist_headers = [
]

cogl_noop_driver_sources = [
  'driver/nop/cogl-attribute-nop-private.h',
  'driver/nop/cogl-attribute-nop.c',
  'driver/nop/cogl-clip-stack-nop-private.h',
  'driver/nop/cogl-clip-stack-nop.c',
  'driver/nop/cogl-driver-nop.c',
  'driver/nop/cogl-nop-framebuffer.c',
  'driver/nop/cogl-nop-framebuffer.h',
  'driver/nop/cogl-texture-2d-nop-private.h',
  'driver/nop/cogl-texture-2d-nop.c',
]

cogl_gl_prototype_headers = [
  'gl-prototypes/cogl-core-functions.h',
  'gl-prototypes/cogl-gles2-functions.h',
  'gl-prototypes/cogl-glsl-functions.h',
  'gl-prototypes/cogl-in-gles-core-functions.h',
  'gl-prototypes/cogl-in-gles2-core-functions.h',
]

cogl_common_driver_sources = [
  'driver/gl/cogl-attribute-gl-private.h',
  'driver/gl/cogl-attribute-gl.c',
  'driver/gl/cogl-bitmap-gl-private.h',
  'driver/gl/cogl-bitmap-gl.c',
  'driver/gl/cogl-buffer-gl-private.h',
  'driver/gl/cogl-buffer-gl.c',
  'driver/gl/cogl-clip-stack-gl-private.h',
  'driver/gl/cogl-clip-stack-gl.c',
  'driver/gl/cogl-framebuffer-gl-private.h',
  'driver/gl/cogl-framebuffer-gl.c',
  'driver/gl/cogl-gl-framebuffer-back.c',
  'driver/gl/cogl-gl-framebuffer-back.h',
  'driver/gl/cogl-gl-framebuffer-fbo.c',
  'driver/gl/cogl-gl-framebuffer-fbo.h',
  'driver/gl/cogl-pipeline-fragend-glsl-private.h',
  'driver/gl/cogl-pipeline-fragend-glsl.c',
  'driver/gl/cogl-pipeline-opengl-private.h',
  'driver/gl/cogl-pipeline-opengl.c',
  'driver/gl/cogl-pipeline-progend-glsl-private.h',
  'driver/gl/cogl-pipeline-progend-glsl.c',
  'driver/gl/cogl-pipeline-vertend-glsl-private.h',
  'driver/gl/cogl-pipeline-vertend-glsl.c',
  'driver/gl/cogl-texture-2d-gl-private.h',
  'driver/gl/cogl-texture-2d-gl.c',
  'driver/gl/cogl-texture-gl-private.h',
  'driver/gl/cogl-texture-gl.c',
  'driver/gl/cogl-util-gl-private.h',
  'driver/gl/cogl-util-gl.c',
]

gl_driver_sources = [
  'driver/gl/gl/cogl-driver-gl.c',
  'driver/gl/gl/cogl-texture-driver-gl.c',
]

gles_driver_sources = [
  'driver/gl/gles/cogl-driver-gles.c',
  'driver/gl/gles/cogl-texture-driver-gles.c',
]

cogl_driver_sources = [
  cogl_noop_driver_sources,
  cogl_common_driver_sources,
]

if have_gl
  cogl_driver_sources += gl_driver_sources
endif

if have_gles2
  cogl_driver_sources += gles_driver_sources
endif

cogl_sources = [
  cogl_driver_sources,
  'cogl-atlas-texture-private.h',
  'cogl-atlas-texture.c',
  'cogl-atlas.c',
  'cogl-atlas.h',
  'cogl-attribute-buffer-private.h',
  'cogl-attribute-buffer.c',
  'cogl-attribute-private.h',
  'cogl-attribute.c',
  'cogl-bitmap-conversion.c',
  'cogl-bitmap-packing.h',
  'cogl-bitmap-private.h',
  'cogl-bitmap.c',
  'cogl-bitmask.c',
  'cogl-bitmask.h',
  'cogl-blend-string.c',
  'cogl-blend-string.h',
  'cogl-blit.c',
  'cogl-blit.h',
  'cogl-boxed-value.c',
  'cogl-boxed-value.h',
  'cogl-buffer-private.h',
  'cogl-buffer.c',
  'cogl-clip-stack.c',
  'cogl-clip-stack.h',
  'cogl-closure-list-private.h',
  'cogl-closure-list.c',
  'cogl-color-private.h',
  'cogl-color.c',
  'cogl-context-private.h',
  'cogl-context.c',
  'cogl-cpu-caps.c',
  'cogl-cpu-caps.h',
  'cogl-debug-options.h',
  'cogl-debug.c',
  'cogl-debug.h',
  'cogl-depth-state-private.h',
  'cogl-depth-state.c',
  'cogl-display-private.h',
  'cogl-display.c',
  'cogl-display.h',
  'cogl-dma-buf-handle.c',
  'cogl-driver.h',
  'cogl-feature-private.c',
  'cogl-feature-private.h',
  'cogl-fence-private.h',
  'cogl-fence.c',
  'cogl-flags.h',
  'cogl-frame-info-private.h',
  'cogl-frame-info.c',
  'cogl-framebuffer-driver.c',
  'cogl-framebuffer-driver.h',
  'cogl-framebuffer-private.h',
  'cogl-framebuffer.c',
  'cogl-glib-source.c',
  'cogl-gl-header.h',
  'cogl-glsl-shader-boilerplate.h',
  'cogl-graphene.c',
  'cogl-half-float.c',
  'cogl-half-float.h',
  'cogl-i18n-private.h',
  'cogl-index-buffer-private.h',
  'cogl-index-buffer.c',
  'cogl-indices-private.h',
  'cogl-indices.c',
  'cogl-journal-private.h',
  'cogl-journal.c',
  'cogl-list.c',
  'cogl-list.h',
  'cogl-magazine-private.h',
  'cogl-magazine.c',
  'cogl-matrix-stack-private.h',
  'cogl-matrix-stack.c',
  'cogl-memory-stack-private.h',
  'cogl-memory-stack.c',
  'cogl-meta-texture.c',
  'cogl-mutter.h',
  'cogl-node-private.h',
  'cogl-node.c',
  'cogl-offscreen-private.h',
  'cogl-offscreen.c',
  'cogl-onscreen-private.h',
  'cogl-onscreen-template-private.h',
  'cogl-onscreen-template.c',
  'cogl-onscreen-template.h',
  'cogl-onscreen.c',
  'cogl-output-private.h',
  'cogl-output.c',
  'cogl-pipeline-cache-private.h',
  'cogl-pipeline-cache.c',
  'cogl-pipeline-cache.h',
  'cogl-pipeline-debug.c',
  'cogl-pipeline-hash-table.c',
  'cogl-pipeline-hash-table.h',
  'cogl-pipeline-layer-private.h',
  'cogl-pipeline-layer-state-private.h',
  'cogl-pipeline-layer-state.c',
  'cogl-pipeline-layer.c',
  'cogl-pipeline-private.h',
  'cogl-pipeline-snippet-private.h',
  'cogl-pipeline-snippet.c',
  'cogl-pipeline-state-private.h',
  'cogl-pipeline-state.c',
  'cogl-pipeline.c',
  'cogl-pixel-buffer-private.h',
  'cogl-pixel-buffer.c',
  'cogl-pixel-format.c',
  'cogl-point-in-poly-private.h',
  'cogl-point-in-poly.c',
  'cogl-poll-private.h',
  'cogl-poll.c',
  'cogl-primitive-private.h',
  'cogl-primitive-texture.c',
  'cogl-primitive.c',
  'cogl-primitives-private.h',
  'cogl-primitives.c',
  'cogl-private.h',
  'cogl-profile.c',
  'cogl-profile.h',
  'cogl-rectangle-map.c',
  'cogl-rectangle-map.h',
  'cogl-renderer-private.h',
  'cogl-renderer.c',
  'cogl-renderer.h',
  'cogl-sampler-cache-private.h',
  'cogl-sampler-cache.c',
  'cogl-scanout.c',
  'cogl-snippet-private.h',
  'cogl-snippet.c',
  'cogl-soft-float.c',
  'cogl-soft-float.h',
  'cogl-spans.c',
  'cogl-spans.h',
  'cogl-sub-texture-private.h',
  'cogl-sub-texture.c',
  'cogl-swap-chain-private.h',
  'cogl-swap-chain.c',
  'cogl-swap-chain.h',
  'cogl-texture-2d-private.h',
  'cogl-texture-2d-sliced-private.h',
  'cogl-texture-2d-sliced.c',
  'cogl-texture-2d.c',
  'cogl-texture-driver.h',
  'cogl-texture-private.h',
  'cogl-texture.c',
  'cogl-trace.c',
  'cogl-util.c',
  'cogl-util.h',
  'cogl.c',
  'deprecated/cogl-clutter.c',
  'deprecated/cogl-program-private.h',
  'deprecated/cogl-program.c',
  'deprecated/cogl-shader-private.h',
  'deprecated/cogl-shader.c',
  'gl-prototypes/cogl-all-functions.h',
  'winsys/cogl-winsys-private.h',
  'winsys/cogl-winsys.c',
]

if have_x11
  cogl_gir_includes += ['xlib-2.0']
  cogl_nonintrospected_headers += [
    'cogl-xlib.h',
    'winsys/cogl-texture-pixmap-x11.h',
  ]
  cogl_sources += [
    'cogl-x11-onscreen.c',
    'cogl-x11-onscreen.h',
    'cogl-x11-renderer-private.h',
    'cogl-xlib-renderer-private.h',
    'cogl-xlib-renderer.c',
    'winsys/cogl-texture-pixmap-x11-private.h',
    'winsys/cogl-texture-pixmap-x11.c',
  ]
  cogl_headers += [
    'cogl-xlib-renderer.h'
  ]
endif

if have_glx
  cogl_nonintrospected_headers += [
    'winsys/cogl-glx.h',
  ]
  cogl_sources += [
    'winsys/cogl-glx-display-private.h',
    'winsys/cogl-glx-renderer-private.h',
    'winsys/cogl-onscreen-glx.c',
    'winsys/cogl-onscreen-glx.h',
    'winsys/cogl-winsys-glx-feature-functions.h',
    'winsys/cogl-winsys-glx-private.h',
    'winsys/cogl-winsys-glx.c',
  ]
endif

if have_egl
  cogl_nonintrospected_headers += [
    'cogl-egl.h',
  ]
  cogl_sources += [
    'cogl-egl-private.h',
    'winsys/cogl-onscreen-egl.c',
    'winsys/cogl-onscreen-egl.h',
    'winsys/cogl-winsys-egl-feature-functions.h',
    'winsys/cogl-winsys-egl-private.h',
    'winsys/cogl-winsys-egl.c',
  ]
endif

if have_egl_xlib
  cogl_sources += [
    'winsys/cogl-onscreen-xlib.c',
    'winsys/cogl-onscreen-xlib.h',
    'winsys/cogl-winsys-egl-x11-private.h',
    'winsys/cogl-winsys-egl-x11.c',
  ]
endif

cogl_enum_headers = [
  'cogl-buffer.h',
  'cogl-pixel-format.h',
]

cogl_headers += [
  cogl_enum_headers,
]

cogl_enums = gnome.mkenums('cogl-enums',
         sources: cogl_enum_headers,
      c_template: 'cogl-enum-types.c.in',
      h_template: 'cogl-enum-types.h.in',
     install_dir: cogl_cogl_includedir,
  install_header: true,
)

cogl_sources += [cogl_enums[0]]

cogl_introspected_headers = [
  cogl_enums[1],
  cogl_headers,
  cogl_deprecated_headers,
]

cogl_headers_all = [
  cogl_introspected_headers,
  cogl_nonintrospected_headers,
  cogl_deprecated_headers,
]

libmutter_cogl_name = 'mutter-cogl-' + libmutter_api_version
libmutter_cogl = shared_library(libmutter_cogl_name,
  sources: [cogl_sources, cogl_headers_all],
  version: '0.0.0',
  soversion: 0,
  c_args: cogl_c_args,
  include_directories: [cogl_includepath, top_includepath],
  dependencies: cogl_deps,
  gnu_symbol_visibility: 'hidden',
  install_rpath: pkglibdir,
  install_dir: pkglibdir,
  install: true,
)
libmutter_cogl_dep = declare_dependency(
  sources: [cogl_enums[1]],
  dependencies: [cogl_deps],
  link_with: libmutter_cogl,
)

if have_introspection
  libmutter_cogl_gir = gnome.generate_gir(libmutter_cogl,
    sources: cogl_introspected_headers,
    nsversion: libmutter_api_version,
    namespace: 'Cogl',
    export_packages: [libmutter_cogl_name],
    includes: cogl_gir_includes,
    dependencies: [cogl_deps],
    extra_args: introspection_args + [
      '-UCOGL_COMPILATION',
      '-D__COGL_H_INSIDE__',
      '-D__COGL_XLIB_H_INSIDE__',
      '-D__COGL_EGL_H_INSIDE__',
      '-D__COGL_GLX_H_INSIDE__',
      '-DCOGL_GIR_SCANNING',
    ],
    header: 'cogl/cogl.h',
    kwargs: introspection_common,
  )
endif

install_headers([
    cogl_headers,
    cogl_nonintrospected_headers,
  ],
  subdir: cogl_cogl_includesubdir)

install_headers([
    cogl_deprecated_headers,
  ],
  subdir: cogl_cogl_includesubdir / 'deprecated')

install_headers(cogl_gl_prototype_headers,
  subdir: cogl_cogl_includesubdir / 'gl-prototypes')

pkg.generate(libmutter_cogl,
  name: 'Cogl',
  filebase: libmutter_cogl_name,
  description: 'An object oriented GL/GLES Abstraction/Utility Layer in mutter',
  libraries: [m_dep],
  subdirs: pkgname / 'cogl',
  requires: [cogl_pkg_deps],
  version: meson.project_version(),
  variables: [
    'apiversion=' + libmutter_api_version,
  ],
  install_dir: pcdir,
)
