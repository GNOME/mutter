/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <gio/gio.h>
#include <stdint.h>

#include "backends/native/meta-kms-types.h"

typedef struct _MetaKmsMonitorControl MetaKmsMonitorControl;

MetaKmsMonitorControl * meta_kms_monitor_control_new (MetaKmsDevice  *device,
                                                       uint32_t        connector_id,
                                                       GError        **error);

void meta_kms_monitor_control_free (MetaKmsMonitorControl *control);

int meta_kms_monitor_control_steal_fd (MetaKmsMonitorControl *control);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsMonitorControl,
                               meta_kms_monitor_control_free)
