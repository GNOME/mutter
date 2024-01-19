/*
 * Copyright (C) 2018 Red Hat
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-kms-impl.h"

#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-update-private.h"

struct _MetaKmsImpl
{
  GObject parent;

  GPtrArray *update_filters;
};

typedef struct _MetaKmsImplPrivate
{
  GList *impl_devices;
} MetaKmsImplPrivate;

struct _MetaKmsUpdateFilter
{
  MetaKmsUpdateFilterFunc func;
  gpointer user_data;
};

G_DEFINE_TYPE_WITH_PRIVATE (MetaKmsImpl, meta_kms_impl, META_TYPE_THREAD_IMPL)

MetaKms *
meta_kms_impl_get_kms (MetaKmsImpl *impl)
{
  MetaThreadImpl *thread_impl = META_THREAD_IMPL (impl);

  return META_KMS (meta_thread_impl_get_thread (thread_impl));
}

void
meta_kms_impl_add_impl_device (MetaKmsImpl       *impl,
                               MetaKmsImplDevice *impl_device)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl));

  priv->impl_devices = g_list_append (priv->impl_devices, impl_device);
}

void
meta_kms_impl_remove_impl_device (MetaKmsImpl       *impl,
                                  MetaKmsImplDevice *impl_device)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl));

  priv->impl_devices = g_list_remove (priv->impl_devices, impl_device);
}

void
meta_kms_impl_discard_pending_page_flips (MetaKmsImpl *impl)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  g_list_foreach (priv->impl_devices,
                  (GFunc) meta_kms_impl_device_discard_pending_page_flips,
                  NULL);
}

void
meta_kms_impl_resume (MetaKmsImpl *impl)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);
  GList *l;

  for (l = priv->impl_devices; l; l = l->next)
    {
      MetaKmsImplDevice *impl_device = l->data;

      meta_kms_impl_device_resume (impl_device);
    }
}

void
meta_kms_impl_prepare_shutdown (MetaKmsImpl *impl)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);
  GList *l;

  for (l = priv->impl_devices; l; l = l->next)
    {
      MetaKmsImplDevice *impl_device = l->data;

      meta_kms_impl_device_discard_pending_page_flips (impl_device);
      meta_kms_impl_device_prepare_shutdown (impl_device);
    }
}

void
meta_kms_impl_notify_modes_set (MetaKmsImpl *impl)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  g_list_foreach (priv->impl_devices,
                  (GFunc) meta_kms_impl_device_notify_modes_set,
                  NULL);
}

static void
meta_kms_update_filter_free (MetaKmsUpdateFilter *filter)
{
  g_free (filter);
}

MetaKmsUpdate *
meta_kms_impl_filter_update (MetaKmsImpl       *impl,
                             MetaKmsCrtc       *crtc,
                             MetaKmsUpdate     *update,
                             MetaKmsUpdateFlag  flags)
{
  int i;

  for (i = 0; i < impl->update_filters->len; i++)
    {
      MetaKmsUpdateFilter *filter = g_ptr_array_index (impl->update_filters, i);

      update = filter->func (impl, crtc, update, flags, filter->user_data);
    }

  return update;
}

MetaKmsImpl *
meta_kms_impl_new (MetaKms *kms)
{
  return g_object_new (META_TYPE_KMS_IMPL,
                       "kms", kms,
                       NULL);
}

static void
meta_kms_impl_init (MetaKmsImpl *impl)
{
  impl->update_filters =
    g_ptr_array_new_with_free_func ((GDestroyNotify) meta_kms_update_filter_free);
}

static void
meta_kms_impl_finalize (GObject *object)
{
  MetaKmsImpl *impl = META_KMS_IMPL (object);

  g_clear_pointer (&impl->update_filters, g_ptr_array_unref);

  G_OBJECT_CLASS (meta_kms_impl_parent_class)->finalize (object);
}

static void
meta_kms_impl_class_init (MetaKmsImplClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_impl_finalize;
}

MetaKmsUpdateFilter *
meta_kms_impl_add_update_filter (MetaKmsImpl             *impl,
                                 MetaKmsUpdateFilterFunc  func,
                                 gpointer                 user_data)
{
  MetaKmsUpdateFilter *filter;

  filter = g_new0 (MetaKmsUpdateFilter, 1);
  filter->func = func;
  filter->user_data = user_data;

  g_ptr_array_add (impl->update_filters, filter);

  return filter;
}

void
meta_kms_impl_remove_update_filter (MetaKmsImpl         *impl,
                                    MetaKmsUpdateFilter *filter)
{
  g_ptr_array_remove (impl->update_filters, filter);
}
