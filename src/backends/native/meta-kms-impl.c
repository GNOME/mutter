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

enum
{
  PROP_0,

  PROP_KMS,
};

struct _MetaKmsImpl
{
  GObject parent;
};

typedef struct _MetaKmsImplPrivate
{
  MetaKms *kms;

  GList *impl_devices;
} MetaKmsImplPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaKmsImpl, meta_kms_impl, G_TYPE_OBJECT)

MetaKms *
meta_kms_impl_get_kms (MetaKmsImpl *impl)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  return priv->kms;
}

void
meta_kms_impl_add_impl_device (MetaKmsImpl       *impl,
                               MetaKmsImplDevice *impl_device)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  meta_assert_in_kms_impl (priv->kms);

  priv->impl_devices = g_list_append (priv->impl_devices, impl_device);
}

void
meta_kms_impl_remove_impl_device (MetaKmsImpl       *impl,
                                  MetaKmsImplDevice *impl_device)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  meta_assert_in_kms_impl (priv->kms);

  priv->impl_devices = g_list_remove (priv->impl_devices, impl_device);
}

MetaKmsFeedback *
meta_kms_impl_process_update (MetaKmsImpl   *impl,
                              MetaKmsUpdate *update)
{
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);
  MetaKmsDevice *device;
  MetaKmsImplDevice *impl_device;

  meta_assert_in_kms_impl (priv->kms);

  device = meta_kms_update_get_device (update);
  impl_device = meta_kms_device_get_impl_device (device);

  return meta_kms_impl_device_process_update (impl_device, update);
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

MetaKmsImpl *
meta_kms_impl_new (MetaKms *kms)
{
  return g_object_new (META_TYPE_KMS_IMPL,
                       "kms", kms,
                       NULL);
}

static void
meta_kms_impl_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  MetaKmsImpl *impl = META_KMS_IMPL (object);
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  switch (prop_id)
    {
    case PROP_KMS:
      priv->kms = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_kms_impl_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  MetaKmsImpl *impl = META_KMS_IMPL (object);
  MetaKmsImplPrivate *priv = meta_kms_impl_get_instance_private (impl);

  switch (prop_id)
    {
    case PROP_KMS:
      g_value_set_object (value, priv->kms);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_kms_impl_init (MetaKmsImpl *kms_impl)
{
}

static void
meta_kms_impl_class_init (MetaKmsImplClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  object_class->set_property = meta_kms_impl_set_property;
  object_class->get_property = meta_kms_impl_get_property;

  pspec = g_param_spec_object ("kms",
                               "kms",
                               "MetaKms",
                               META_TYPE_KMS,
                               G_PARAM_READWRITE |
                               G_PARAM_STATIC_STRINGS |
                               G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class,
                                   PROP_KMS,
                                   pspec);
}
