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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "backends/native/meta-kms-page-flip-private.h"

#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-update.h"

typedef struct _MetaKmsPageFlipClosure
{
  const MetaKmsPageFlipListenerVtable *vtable;
  GMainContext *main_context;
  gpointer user_data;
  GDestroyNotify destroy_notify;

  MetaKmsPageFlipData *page_flip_data;
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
                                GMainContext                        *main_context,
                                gpointer                             user_data,
                                GDestroyNotify                       destroy_notify)
{
  MetaKmsPageFlipClosure *closure;

  closure = g_new0 (MetaKmsPageFlipClosure, 1);
  *closure = (MetaKmsPageFlipClosure) {
    .vtable = vtable,
    .main_context = main_context,
    .user_data = user_data,
    .destroy_notify = destroy_notify,
  };

  return closure;
}

static void
meta_kms_page_flip_closure_free (MetaKmsPageFlipClosure *closure)
{
  g_clear_pointer (&closure->page_flip_data, meta_kms_page_flip_data_unref);
  if (closure->destroy_notify)
    g_clear_pointer (&closure->user_data, closure->destroy_notify);
  g_free (closure);
}

static void
meta_kms_page_closure_set_data (MetaKmsPageFlipClosure *closure,
                                MetaKmsPageFlipData    *page_flip_data)
{
  g_return_if_fail (!closure->page_flip_data);

  closure->page_flip_data = meta_kms_page_flip_data_ref (page_flip_data);
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
                                      GMainContext                        *main_context,
                                      gpointer                             user_data,
                                      GDestroyNotify                       destroy_notify)
{
  MetaKmsPageFlipClosure *closure;

  closure = meta_kms_page_flip_closure_new (vtable,
                                            main_context,
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
invoke_page_flip_closure_flipped (MetaThread *thread,
                                  gpointer    user_data)
{
  MetaKmsPageFlipClosure *closure = user_data;
  MetaKmsPageFlipData *page_flip_data = closure->page_flip_data;

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

  meta_topic (META_DEBUG_KMS,
              "Setting page flip timings for CRTC (%u, %s), sequence: %u, sec: %u, usec: %u",
              meta_kms_crtc_get_id (page_flip_data->crtc),
              meta_kms_impl_device_get_path (page_flip_data->impl_device),
              sequence, sec, usec);

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
  g_autoptr (GList) closures = NULL;
  GList *l;

  meta_assert_in_kms_impl (kms);

  closures = g_steal_pointer (&page_flip_data->closures);
  for (l = closures; l; l = l->next)
    {
      MetaKmsPageFlipClosure *closure = l->data;

      meta_kms_page_closure_set_data (closure, page_flip_data);
      meta_kms_queue_callback (kms,
                               closure->main_context,
                               invoke_page_flip_closure_flipped,
                               closure,
                               (GDestroyNotify) meta_kms_page_flip_closure_free);

    }

  meta_kms_page_flip_data_unref (page_flip_data);
}

static void
invoke_page_flip_closure_mode_set_fallback (MetaThread *thread,
                                            gpointer    user_data)
{
  MetaKmsPageFlipClosure *closure = user_data;
  MetaKmsPageFlipData *page_flip_data = closure->page_flip_data;

  closure->vtable->mode_set_fallback (page_flip_data->crtc,
                                      closure->user_data);
}

void
meta_kms_page_flip_data_mode_set_fallback_in_impl (MetaKmsPageFlipData *page_flip_data)
{
  MetaKms *kms = meta_kms_from_impl_device (page_flip_data->impl_device);
  g_autoptr (GList) closures = NULL;
  GList *l;

  meta_assert_in_kms_impl (kms);

  closures = g_steal_pointer (&page_flip_data->closures);
  for (l = closures; l; l = l->next)
    {
      MetaKmsPageFlipClosure *closure = l->data;

      meta_kms_page_closure_set_data (closure, page_flip_data);
      meta_kms_queue_callback (kms,
                               closure->main_context,
                               invoke_page_flip_closure_mode_set_fallback,
                               closure,
                               (GDestroyNotify) meta_kms_page_flip_closure_free);
    }

  meta_kms_page_flip_data_unref (page_flip_data);
}

static void
invoke_page_flip_closure_discarded (MetaThread *thread,
                                    gpointer    user_data)
{
  MetaKmsPageFlipClosure *closure = user_data;
  MetaKmsPageFlipData *page_flip_data = closure->page_flip_data;

  closure->vtable->discarded (page_flip_data->crtc,
                              closure->user_data,
                              page_flip_data->error);
}

static void
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
  g_autoptr (GList) closures = NULL;
  GList *l;

  meta_assert_in_kms_impl (kms);

  if (error)
    meta_kms_page_flip_data_take_error (page_flip_data, g_error_copy (error));

  closures = g_steal_pointer (&page_flip_data->closures);
  for (l = closures; l; l = l->next)
    {
      MetaKmsPageFlipClosure *closure = l->data;

      meta_kms_page_closure_set_data (closure, page_flip_data);

      meta_kms_queue_callback (kms,
                               closure->main_context,
                               invoke_page_flip_closure_discarded,
                               closure,
                               (GDestroyNotify) meta_kms_page_flip_closure_free);
    }

  meta_kms_page_flip_data_unref (page_flip_data);
}
