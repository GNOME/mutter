/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter main() */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2006 Elijah Newren
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

/**
 * SECTION:main
 * @title: Main
 * @short_description: Program startup.
 *
 * Functions which parse the command-line arguments, create the display,
 * kick everything off and then close down Mutter when it's time to go.
 *
 *
 *
 * Mutter - a boring window manager for the adult in you
 *
 * Many window managers are like Marshmallow Froot Loops; Mutter
 * is like Frosted Flakes: it's still plain old corn, but dusted
 * with some sugar.
 *
 * The best way to get a handle on how the whole system fits together
 * is discussed in doc/code-overview.txt; if you're looking for functions
 * to investigate, read main(), meta_display_open(), and event_callback().
 */

#include "config.h"

#include "meta/main.h"

#include "backends/x11/cm/meta-backend-x11-cm.h"
#include "core/meta-context-private.h"
#include "core/main-private.h"

#if defined(HAVE_NATIVE_BACKEND) && defined(HAVE_WAYLAND)
#include <systemd/sd-login.h>
#endif /* HAVE_WAYLAND && HAVE_NATIVE_BACKEND */

MetaContext *
meta_get_context_temporary (void);

/**
 * meta_quit:
 * @code: The success or failure code to return to the calling process.
 *
 * Stops Mutter. This tells the event loop to stop processing; it is
 * rather dangerous to use this because this will leave the user with
 * no window manager. We generally do this only if, for example, the
 * session manager asks us to; we assume the session manager knows
 * what it's talking about.
 */
void
meta_quit (MetaExitCode code)
{
  MetaContext *context;

  context = meta_get_context_temporary ();
  g_assert (context);

  if (code == META_EXIT_SUCCESS)
    {
      meta_context_terminate (context);
    }
}

static MetaX11DisplayPolicy x11_display_policy_override = -1;

void
meta_override_x11_display_policy (MetaX11DisplayPolicy x11_display_policy)
{
  x11_display_policy_override = x11_display_policy;
}

MetaX11DisplayPolicy
meta_get_x11_display_policy (void)
{
  MetaBackend *backend = meta_get_backend ();

  if (META_IS_BACKEND_X11_CM (backend))
    return META_X11_DISPLAY_POLICY_MANDATORY;

  if (x11_display_policy_override != -1)
    return x11_display_policy_override;

#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    {
#ifdef HAVE_XWAYLAND_INITFD
      g_autofree char *unit = NULL;
#endif

#ifdef HAVE_XWAYLAND_INITFD
      if (sd_pid_get_user_unit (0, &unit) < 0)
        return META_X11_DISPLAY_POLICY_MANDATORY;
      else
        return META_X11_DISPLAY_POLICY_ON_DEMAND;
#endif
    }
#endif

  return META_X11_DISPLAY_POLICY_MANDATORY;
}
