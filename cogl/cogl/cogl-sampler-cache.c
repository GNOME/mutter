/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation.
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
 *
 *
 * Authors:
 *   Neil Roberts <neil@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-sampler-cache-private.h"
#include "cogl/cogl-context-private.h"

struct _CoglSamplerCache
{
  CoglContext *context;

  /* The samplers are hashed in two tables. One is using the enum
     values that Cogl exposes (so it can include the 'automatic' wrap
     mode) and the other is using the converted values that will be
     given to GL. The first is used to get a unique pointer for the
     sampler state so that pipelines only need to store a single
     pointer instead of the whole state and the second is used so that
     only a single GL sampler object will be created for each unique
     GL state. */
  GHashTable *hash_table_cogl;
  GHashTable *hash_table_gl;
};

static CoglSamplerCacheWrapMode
get_real_wrap_mode (CoglSamplerCacheWrapMode wrap_mode)
{
  if (wrap_mode == COGL_SAMPLER_CACHE_WRAP_MODE_AUTOMATIC)
    return COGL_SAMPLER_CACHE_WRAP_MODE_CLAMP_TO_EDGE;

  return wrap_mode;
}

static void
canonicalize_key (CoglSamplerCacheEntry *key)
{
  /* This converts the wrap modes to the enums that will actually be
     given to GL so that it can be used as a key to get a unique GL
     sampler object for the state */
  key->wrap_mode_s = get_real_wrap_mode (key->wrap_mode_s);
  key->wrap_mode_t = get_real_wrap_mode (key->wrap_mode_t);
}

static gboolean
wrap_mode_equal_gl (CoglSamplerCacheWrapMode wrap_mode0,
                    CoglSamplerCacheWrapMode wrap_mode1)
{
  /* We want to compare the actual GLenum that will be used so that if
     two different wrap_modes actually use the same GL state we'll
     still use the same sampler object */
  return get_real_wrap_mode (wrap_mode0) == get_real_wrap_mode (wrap_mode1);
}

static gboolean
sampler_state_equal_gl (const void *value0,
                        const void *value1)
{
  const CoglSamplerCacheEntry *state0 = value0;
  const CoglSamplerCacheEntry *state1 = value1;

  return (state0->mag_filter == state1->mag_filter &&
          state0->min_filter == state1->min_filter &&
          wrap_mode_equal_gl (state0->wrap_mode_s, state1->wrap_mode_s) &&
          wrap_mode_equal_gl (state0->wrap_mode_t, state1->wrap_mode_t));
}

static unsigned int
hash_wrap_mode_gl (unsigned int hash,
                   CoglSamplerCacheWrapMode wrap_mode)
{
  /* We want to hash the actual GLenum that will be used so that if
     two different wrap_modes actually use the same GL state we'll
     still use the same sampler object */
  GLenum real_wrap_mode = get_real_wrap_mode (wrap_mode);

  return _cogl_util_one_at_a_time_hash (hash,
                                        &real_wrap_mode,
                                        sizeof (real_wrap_mode));
}

static unsigned int
hash_sampler_state_gl (const void *key)
{
  const CoglSamplerCacheEntry *entry = key;
  unsigned int hash = 0;

  hash = _cogl_util_one_at_a_time_hash (hash, &entry->mag_filter,
                                        sizeof (entry->mag_filter));
  hash = _cogl_util_one_at_a_time_hash (hash, &entry->min_filter,
                                        sizeof (entry->min_filter));
  hash = hash_wrap_mode_gl (hash, entry->wrap_mode_s);
  hash = hash_wrap_mode_gl (hash, entry->wrap_mode_t);

  return _cogl_util_one_at_a_time_mix (hash);
}

static gboolean
sampler_state_equal_cogl (const void *value0,
                          const void *value1)
{
  const CoglSamplerCacheEntry *state0 = value0;
  const CoglSamplerCacheEntry *state1 = value1;

  return (state0->mag_filter == state1->mag_filter &&
          state0->min_filter == state1->min_filter &&
          state0->wrap_mode_s == state1->wrap_mode_s &&
          state0->wrap_mode_t == state1->wrap_mode_t);
}

static unsigned int
hash_sampler_state_cogl (const void *key)
{
  const CoglSamplerCacheEntry *entry = key;
  unsigned int hash = 0;

  hash = _cogl_util_one_at_a_time_hash (hash, &entry->mag_filter,
                                        sizeof (entry->mag_filter));
  hash = _cogl_util_one_at_a_time_hash (hash, &entry->min_filter,
                                        sizeof (entry->min_filter));
  hash = _cogl_util_one_at_a_time_hash (hash, &entry->wrap_mode_s,
                                        sizeof (entry->wrap_mode_s));
  hash = _cogl_util_one_at_a_time_hash (hash, &entry->wrap_mode_t,
                                        sizeof (entry->wrap_mode_t));

  return _cogl_util_one_at_a_time_mix (hash);
}

CoglSamplerCache *
_cogl_sampler_cache_new (CoglContext *context)
{
  CoglSamplerCache *cache = g_new (CoglSamplerCache, 1);

  /* No reference is taken on the context because it would create a
     circular reference */
  cache->context = context;

  cache->hash_table_gl = g_hash_table_new (hash_sampler_state_gl,
                                           sampler_state_equal_gl);
  cache->hash_table_cogl = g_hash_table_new (hash_sampler_state_cogl,
                                             sampler_state_equal_cogl);

  return cache;
}

static CoglSamplerCacheEntry *
_cogl_sampler_cache_get_entry_gl (CoglSamplerCache *cache,
                                  const CoglSamplerCacheEntry *key)
{
  CoglSamplerCacheEntry *entry;
  CoglDriver *driver = cogl_context_get_driver (cache->context);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  entry = g_hash_table_lookup (cache->hash_table_gl, key);

  if (entry == NULL)
    {
      entry = g_memdup2 (key, sizeof (CoglSamplerCacheEntry));

      driver_klass->sampler_init (driver, entry);

      g_hash_table_insert (cache->hash_table_gl, entry, entry);
    }

  return entry;
}

static CoglSamplerCacheEntry *
_cogl_sampler_cache_get_entry_cogl (CoglSamplerCache *cache,
                                    const CoglSamplerCacheEntry *key)
{
  CoglSamplerCacheEntry *entry;

  entry = g_hash_table_lookup (cache->hash_table_cogl, key);

  if (entry == NULL)
    {
      CoglSamplerCacheEntry canonical_key;
      CoglSamplerCacheEntry *gl_entry;

      entry = g_memdup2 (key, sizeof (CoglSamplerCacheEntry));

      /* Get the sampler object number from the canonical GL version
         of the sampler state cache */
      canonical_key = *key;
      canonicalize_key (&canonical_key);
      gl_entry = _cogl_sampler_cache_get_entry_gl (cache, &canonical_key);
      entry->sampler_object = gl_entry->sampler_object;

      g_hash_table_insert (cache->hash_table_cogl, entry, entry);
    }

  return entry;
}

const CoglSamplerCacheEntry *
_cogl_sampler_cache_get_default_entry (CoglSamplerCache *cache)
{
  CoglSamplerCacheEntry key = { 0, };

  key.wrap_mode_s = COGL_SAMPLER_CACHE_WRAP_MODE_AUTOMATIC;
  key.wrap_mode_t = COGL_SAMPLER_CACHE_WRAP_MODE_AUTOMATIC;

  key.min_filter = GL_LINEAR;
  key.mag_filter = GL_LINEAR;

  return _cogl_sampler_cache_get_entry_cogl (cache, &key);
}

const CoglSamplerCacheEntry *
_cogl_sampler_cache_update_wrap_modes (CoglSamplerCache *cache,
                                       const CoglSamplerCacheEntry *old_entry,
                                       CoglSamplerCacheWrapMode wrap_mode_s,
                                       CoglSamplerCacheWrapMode wrap_mode_t)
{
  CoglSamplerCacheEntry key = *old_entry;

  key.wrap_mode_s = wrap_mode_s;
  key.wrap_mode_t = wrap_mode_t;

  return _cogl_sampler_cache_get_entry_cogl (cache, &key);
}

const CoglSamplerCacheEntry *
_cogl_sampler_cache_update_filters (CoglSamplerCache *cache,
                                    const CoglSamplerCacheEntry *old_entry,
                                    GLenum min_filter,
                                    GLenum mag_filter)
{
  CoglSamplerCacheEntry key = *old_entry;

  key.min_filter = min_filter;
  key.mag_filter = mag_filter;

  return _cogl_sampler_cache_get_entry_cogl (cache, &key);
}

static void
hash_table_free_gl_cb (void *key,
                       void *value,
                       void *user_data)
{
  CoglContext *context = user_data;
  CoglSamplerCacheEntry *entry = value;
  CoglDriver *driver = cogl_context_get_driver (context);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  driver_klass->sampler_free (driver, entry);

  g_free (entry);
}

static void
hash_table_free_cogl_cb (void *key,
                         void *value,
                         void *user_data)
{
  CoglSamplerCacheEntry *entry = value;

  g_free (entry);
}

void
_cogl_sampler_cache_free (CoglSamplerCache *cache)
{
  g_hash_table_foreach (cache->hash_table_gl,
                        hash_table_free_gl_cb,
                        cache->context);

  g_hash_table_destroy (cache->hash_table_gl);

  g_hash_table_foreach (cache->hash_table_cogl,
                        hash_table_free_cogl_cb,
                        cache->context);

  g_hash_table_destroy (cache->hash_table_cogl);

  g_free (cache);
}
