/*
 * Copyright (C) 2019 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "backends/native/meta-kms-page-flip-private.h"

#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-update.h"

typedef struct _MetaKmsPageFlipClosure
{
  const MetaKmsPageFlipListenerVtable *vtable;
  MetaKmsPageFlipListenerFlag flags;
  gpointer user_data;
  GDestroyNotify destroy_notify;
} MetaKmsPageFlipClosure;

struct _MetaKmsPageFlipData
{
  gatomicrefcount ref_count;

  MetaKmsImplDevice *impl_device;
  MetaKmsCrtc *crtc;

  GList *closures;

  unsigned int sequence;
  unsigned int sec;
  unsigned int usec;

  gboolean is_symbolic;

  GError *error;
};

static MetaKmsPageFlipClosure *
meta_kms_page_flip_closure_new (const MetaKmsPageFlipListenerVtable *vtable,
                                MetaKmsPageFlipListenerFlag          flags,
                                gpointer                             user_data,
                                GDestroyNotify                       destroy_notify)
{
  MetaKmsPageFlipClosure *closure;

  closure = g_new0 (MetaKmsPageFlipClosure, 1);
  *closure = (MetaKmsPageFlipClosure) {
    .vtable = vtable,
    .flags = flags,
    .user_data = user_data,
    .destroy_notify = destroy_notify,
  };

  return closure;
}

static void
meta_kms_page_flip_closure_free (MetaKmsPageFlipClosure *closure)
{
  g_clear_pointer (&closure->user_data, closure->destroy_notify);
  g_free (closure);
}

MetaKmsPageFlipData *
meta_kms_page_flip_data_new (MetaKmsImplDevice *impl_device,
                             MetaKmsCrtc       *crtc)
{
  MetaKmsPageFlipData *page_flip_data;

  page_flip_data = g_new0 (MetaKmsPageFlipData , 1);
  *page_flip_data = (MetaKmsPageFlipData) {
    .impl_device = impl_device,
    .crtc = crtc,
  };
  g_atomic_ref_count_init (&page_flip_data->ref_count);

  return page_flip_data;
}

MetaKmsPageFlipData *
meta_kms_page_flip_data_ref (MetaKmsPageFlipData *page_flip_data)
{
  g_atomic_ref_count_inc (&page_flip_data->ref_count);

  return page_flip_data;
}

void
meta_kms_page_flip_data_unref (MetaKmsPageFlipData *page_flip_data)
{
  if (g_atomic_ref_count_dec (&page_flip_data->ref_count))
    {
      g_list_free_full (page_flip_data->closures,
                        (GDestroyNotify) meta_kms_page_flip_closure_free);
      g_clear_error (&page_flip_data->error);
      g_free (page_flip_data);
    }
}

void
meta_kms_page_flip_data_add_listener (MetaKmsPageFlipData                 *page_flip_data,
                                      const MetaKmsPageFlipListenerVtable *vtable,
                                      MetaKmsPageFlipListenerFlag          flags,
                                      gpointer                             user_data,
                                      GDestroyNotify                       destroy_notify)
{
  MetaKmsPageFlipClosure *closure;

  closure = meta_kms_page_flip_closure_new (vtable, flags,
                                            user_data,
                                            destroy_notify);
  page_flip_data->closures = g_list_append (page_flip_data->closures, closure);
}

MetaKmsImplDevice *
meta_kms_page_flip_data_get_impl_device (MetaKmsPageFlipData *page_flip_data)
{
  return page_flip_data->impl_device;
}

MetaKmsCrtc *
meta_kms_page_flip_data_get_crtc (MetaKmsPageFlipData *page_flip_data)
{
  return page_flip_data->crtc;
}

static void
meta_kms_page_flip_data_flipped (MetaKms  *kms,
                                 gpointer  user_data)
{
  MetaKmsPageFlipData *page_flip_data = user_data;
  GList *l;

  meta_assert_not_in_kms_impl (kms);

  for (l = page_flip_data->closures; l; l = l->next)
    {
      MetaKmsPageFlipClosure *closure = l->data;

      if (page_flip_data->is_symbolic)
        {
          closure->vtable->ready (page_flip_data->crtc,
                                  closure->user_data);
        }
      else
        {
          closure->vtable->flipped (page_flip_data->crtc,
                                    page_flip_data->sequence,
                                    page_flip_data->sec,
                                    page_flip_data->usec,
                                    closure->user_data);
        }
    }
}

static MetaKms *
meta_kms_from_impl_device (MetaKmsImplDevice *impl_device)
{
  MetaKmsDevice *device = meta_kms_impl_device_get_device (impl_device);

  return meta_kms_device_get_kms (device);
}

void
meta_kms_page_flip_data_set_timings_in_impl (MetaKmsPageFlipData *page_flip_data,
                                             unsigned int         sequence,
                                             unsigned int         sec,
                                             unsigned int         usec)
{
  MetaKms *kms = meta_kms_from_impl_device (page_flip_data->impl_device);

  meta_assert_in_kms_impl (kms);

  page_flip_data->sequence = sequence;
  page_flip_data->sec = sec;
  page_flip_data->usec = usec;
}

void
meta_kms_page_flip_data_make_symbolic (MetaKmsPageFlipData *page_flip_data)
{
  page_flip_data->is_symbolic = TRUE;
}

void
meta_kms_page_flip_data_flipped_in_impl (MetaKmsPageFlipData *page_flip_data)
{
  MetaKms *kms = meta_kms_from_impl_device (page_flip_data->impl_device);

  meta_assert_in_kms_impl (kms);

  meta_kms_queue_callback (kms,
                           meta_kms_page_flip_data_flipped,
                           page_flip_data,
                           (GDestroyNotify) meta_kms_page_flip_data_unref);
}

static void
meta_kms_page_flip_data_mode_set_fallback (MetaKms  *kms,
                                           gpointer  user_data)
{
  MetaKmsPageFlipData *page_flip_data = user_data;
  GList *l;

  meta_assert_not_in_kms_impl (kms);

  for (l = page_flip_data->closures; l; l = l->next)
    {
      MetaKmsPageFlipClosure *closure = l->data;

      closure->vtable->mode_set_fallback (page_flip_data->crtc,
                                          closure->user_data);
    }
}

void
meta_kms_page_flip_data_mode_set_fallback_in_impl (MetaKmsPageFlipData *page_flip_data)
{
  MetaKms *kms = meta_kms_from_impl_device (page_flip_data->impl_device);

  meta_assert_in_kms_impl (kms);

  meta_kms_queue_callback (kms,
                           meta_kms_page_flip_data_mode_set_fallback,
                           page_flip_data,
                           (GDestroyNotify) meta_kms_page_flip_data_unref);
}

static void
meta_kms_page_flip_data_discard (MetaKms  *kms,
                                 gpointer  user_data)
{
  MetaKmsPageFlipData *page_flip_data = user_data;
  GList *l;

  meta_assert_not_in_kms_impl (kms);

  for (l = page_flip_data->closures; l; l = l->next)
    {
      MetaKmsPageFlipClosure *closure = l->data;

      if (closure->flags & META_KMS_PAGE_FLIP_LISTENER_FLAG_NO_DISCARD)
        continue;

      closure->vtable->discarded (page_flip_data->crtc,
                                  closure->user_data,
                                  page_flip_data->error);
    }
}

void
meta_kms_page_flip_data_take_error (MetaKmsPageFlipData *page_flip_data,
                                    GError              *error)
{
  g_assert (!page_flip_data->error);

  page_flip_data->error = error;
}

void
meta_kms_page_flip_data_discard_in_impl (MetaKmsPageFlipData *page_flip_data,
                                         const GError        *error)
{
  MetaKms *kms = meta_kms_from_impl_device (page_flip_data->impl_device);

  meta_assert_in_kms_impl (kms);

  if (error)
    meta_kms_page_flip_data_take_error (page_flip_data, g_error_copy (error));

  meta_kms_queue_callback (kms,
                           meta_kms_page_flip_data_discard,
                           page_flip_data,
                           (GDestroyNotify) meta_kms_page_flip_data_unref);
}
