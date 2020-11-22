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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "backends/native/meta-kms-impl.h"

#include "backends/native/meta-kms-private.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-update-private.h"

struct _MetaKmsImpl
{
  GObject parent;
};

typedef struct _MetaKmsImplPrivate
{
  GList *impl_devices;
} MetaKmsImplPrivate;

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

MetaKmsImpl *
meta_kms_impl_new (MetaKms *kms)
{
  return g_object_new (META_TYPE_KMS_IMPL,
                       "kms", kms,
                       NULL);
}

static void
meta_kms_impl_init (MetaKmsImpl *kms_impl)
{
}

static void
meta_kms_impl_class_init (MetaKmsImplClass *klass)
{
}
