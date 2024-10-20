/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

/* These macros are used to mark deprecated functions, and thus have
 * to be exposed in a public header.
 *
 * They are only intended for internal use and should not be used by
 * other projects.
 */
#if defined(COGL_DISABLE_DEPRECATION_WARNINGS) || defined(COGL_COMPILATION)

#define COGL_DEPRECATED
#define COGL_DEPRECATED_FOR(f)

#else /* COGL_DISABLE_DEPRECATION_WARNINGS */

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
#define COGL_DEPRECATED __attribute__((__deprecated__))
#elif defined(_MSC_VER) && (_MSC_VER >= 1300)
#define COGL_DEPRECATED __declspec(deprecated)
#else
#define COGL_DEPRECATED
#endif

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
#define COGL_DEPRECATED_FOR(f) __attribute__((__deprecated__("Use '" #f "' instead")))
#elif defined(_MSC_FULL_VER) && (_MSC_FULL_VER > 140050320)
#define COGL_DEPRECATED_FOR(f) __declspec(deprecated("is deprecated. Use '" #f "' instead"))
#else
#define COGL_DEPRECATED_FOR(f) G_DEPRECATED
#endif

#endif /* COGL_DISABLE_DEPRECATION_WARNINGS */

#define COGL_EXPORT __attribute__((visibility("default"))) extern
#define COGL_EXPORT_TEST __attribute__((visibility("default"))) extern


/*<private>
 * COGL_DECLARE_INTERNAL_TYPE:
 * @ModuleObjName: The name of the new type, in camel case (like CoglTexture)
 * @module_obj_name: The name of the new type in lowercase, with words
 *  separated by '_' (like 'cogl_texture')
 * @MODULE: The name of the module, in all caps (like 'COGL')
 * @OBJ_NAME: The bare name of the type, in all caps (like 'TEXTURE')
 * @ParentName: the name of the parent type, in camel case (like CoglTexture)
 *
 * A convenience macro for emitting the usual declarations in the
 * header file for a type which is intended to be subclassed only
 * by internal consumers.
 *
 * This macro differs from %G_DECLARE_DERIVABLE_TYPE and %G_DECLARE_FINAL_TYPE
 * by declaring a type that is only derivable internally. Internal users can
 * derive this type, assuming they have access to the instance and class
 * structures; external users will not be able to subclass this type.
 */
#define COGL_DECLARE_INTERNAL_TYPE(ModuleObjName, module_obj_name, MODULE, OBJ_NAME, ParentName) \
        GType module_obj_name##_get_type (void);                                                               \
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS                                                                       \
        typedef struct _##ModuleObjName ModuleObjName;                                                         \
        typedef struct _##ModuleObjName##Class ModuleObjName##Class;                                           \
                                                                                                         \
        _GLIB_DEFINE_AUTOPTR_CHAINUP (ModuleObjName, ParentName)                                               \
        G_DEFINE_AUTOPTR_CLEANUP_FUNC (ModuleObjName##Class, g_type_class_unref)                               \
                                                                                                         \
        G_GNUC_UNUSED static inline ModuleObjName *MODULE##_##OBJ_NAME (gpointer ptr) {                       \
          return G_TYPE_CHECK_INSTANCE_CAST (ptr, module_obj_name##_get_type (), ModuleObjName); }             \
        G_GNUC_UNUSED static inline ModuleObjName##Class *MODULE##_##OBJ_NAME##_CLASS (gpointer ptr) {        \
          return G_TYPE_CHECK_CLASS_CAST (ptr, module_obj_name##_get_type (), ModuleObjName##Class); }         \
        G_GNUC_UNUSED static inline gboolean MODULE##_IS_##OBJ_NAME (gpointer ptr) {                           \
          return G_TYPE_CHECK_INSTANCE_TYPE (ptr, module_obj_name##_get_type ()); }                            \
        G_GNUC_UNUSED static inline gboolean MODULE##_IS_##OBJ_NAME##_CLASS (gpointer ptr) {                   \
          return G_TYPE_CHECK_CLASS_TYPE (ptr, module_obj_name##_get_type ()); }                               \
        G_GNUC_UNUSED static inline ModuleObjName##Class *MODULE##_##OBJ_NAME##_GET_CLASS (gpointer ptr) {    \
          return G_TYPE_INSTANCE_GET_CLASS (ptr, module_obj_name##_get_type (), ModuleObjName##Class); }       \
        G_GNUC_END_IGNORE_DEPRECATIONS
