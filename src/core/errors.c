/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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

/**
 * SECTION:errors
 * @title: Errors
 * @short_description: Mutter X error handling
 */

#include <config.h>
#include <meta/errors.h>
#include "display-private.h"

/* This is a copy-paste of the error handling code in GDK, modified
 * so that it works with mutter's internal structures, since we don't
 * have a GDK display open. */

/* compare X sequence numbers handling wraparound */
#define SEQUENCE_COMPARE(a,op,b) (((long) (a) - (long) (b)) op 0)

typedef struct _GdkErrorTrap  GdkErrorTrap;

struct _GdkErrorTrap
{
  /* Next sequence when trap was pushed, i.e. first sequence to
   * ignore
   */
  gulong start_sequence;

  /* Next sequence when trap was popped, i.e. first sequence
   * to not ignore. 0 if trap is still active.
   */
  gulong end_sequence;

  /* Most recent error code within the sequence */
  int error_code;
};

/* delivers an error event from the error handler in gdkmain-x11.c */
static void
meta_display_error_event (MetaDisplay *display,
                          XErrorEvent *error)
{
  GSList *tmp_list;
  gboolean ignore;

  ignore = FALSE;
  for (tmp_list = display->error_traps;
       tmp_list != NULL;
       tmp_list = tmp_list->next)
    {
      GdkErrorTrap *trap;

      trap = tmp_list->data;

      if (SEQUENCE_COMPARE (trap->start_sequence, <=, error->serial) &&
          (trap->end_sequence == 0 ||
           SEQUENCE_COMPARE (trap->end_sequence, >, error->serial)))
        {
          ignore = TRUE;
          trap->error_code = error->error_code;
          break; /* only innermost trap gets the error code */
        }
    }

  if (!ignore)
    {
      gchar buf[64];
      gchar *msg;

      XGetErrorText (display->xdisplay, error->error_code, buf, 63);

      msg =
        g_strdup_printf ("mutter received an X Window System error: %s\n"
                         "  (Details: serial %ld error_code %d request_code %d minor_code %d)\n",
                         buf,
                         error->serial,
                         error->error_code,
                         error->request_code,
                         error->minor_code);

      g_error ("%s", msg);
    }
}

static int
gdk_x_error (Display	 *xdisplay,
             XErrorEvent *error)
{
  MetaDisplay *display = meta_display_for_x_display (xdisplay);
  meta_display_error_event (display, error);
  return 0;
}

/* non-GDK previous error handler */
typedef int (*GdkXErrorHandler) (Display *, XErrorEvent *);
static GdkXErrorHandler _gdk_old_error_handler;
/* number of times we've pushed the GDK error handler */
static int _gdk_error_handler_push_count = 0;

static void
_gdk_x11_error_handler_push (void)
{
  GdkXErrorHandler previous;

  previous = XSetErrorHandler (gdk_x_error);

  if (_gdk_error_handler_push_count > 0)
    {
      if (previous != gdk_x_error)
        g_warning ("XSetErrorHandler() called with a GDK error trap pushed. Don't do that.");
    }
  else
    {
      _gdk_old_error_handler = previous;
    }

  _gdk_error_handler_push_count += 1;
}

static void
_gdk_x11_error_handler_pop  (void)
{
  g_return_if_fail (_gdk_error_handler_push_count > 0);

  _gdk_error_handler_push_count -= 1;

  if (_gdk_error_handler_push_count == 0)
    {
      XSetErrorHandler (_gdk_old_error_handler);
      _gdk_old_error_handler = NULL;
    }
}

static void
delete_outdated_error_traps (MetaDisplay *display)
{
  GSList *tmp_list;
  gulong processed_sequence;

  processed_sequence = XLastKnownRequestProcessed (display->xdisplay);

  tmp_list = display->error_traps;
  while (tmp_list != NULL)
    {
      GdkErrorTrap *trap = tmp_list->data;

      if (trap->end_sequence != 0 &&
          SEQUENCE_COMPARE (trap->end_sequence, <=, processed_sequence))
        {
          GSList *free_me = tmp_list;

          tmp_list = tmp_list->next;
          display->error_traps = g_slist_delete_link (display->error_traps, free_me);
          g_slice_free (GdkErrorTrap, trap);
        }
      else
        {
          tmp_list = tmp_list->next;
        }
    }
}

void
meta_error_trap_push (MetaDisplay *display)
{
  GdkErrorTrap *trap;

  delete_outdated_error_traps (display);

  /* set up the Xlib callback to tell us about errors */
  _gdk_x11_error_handler_push ();

  trap = g_slice_new0 (GdkErrorTrap);

  trap->start_sequence = XNextRequest (display->xdisplay);
  trap->error_code = Success;

  display->error_traps =
    g_slist_prepend (display->error_traps, trap);
}

static gint
meta_error_trap_pop_internal (MetaDisplay *display,
                              gboolean     need_code)
{
  GdkErrorTrap *trap;
  GSList *tmp_list;
  int result;

  g_return_val_if_fail (display->error_traps != NULL, Success);

  /* Find the first trap that hasn't been popped already */
  trap = NULL; /* quiet gcc */
  for (tmp_list = display->error_traps;
       tmp_list != NULL;
       tmp_list = tmp_list->next)
    {
      trap = tmp_list->data;

      if (trap->end_sequence == 0)
        break;
    }

  g_return_val_if_fail (trap != NULL, Success);
  g_assert (trap->end_sequence == 0);

  /* May need to sync to fill in trap->error_code if we care about
   * getting an error code.
   */
  if (need_code)
    {
      gulong processed_sequence;
      gulong next_sequence;

      next_sequence = XNextRequest (display->xdisplay);
      processed_sequence = XLastKnownRequestProcessed (display->xdisplay);

      /* If our last request was already processed, there is no point
       * in syncing. i.e. if last request was a round trip (or even if
       * we got an event with the serial of a non-round-trip)
       */
      if ((next_sequence - 1) != processed_sequence)
        {
          XSync (display->xdisplay, False);
        }

      result = trap->error_code;
    }
  else
    {
      result = Success;
    }

  /* record end of trap, giving us a range of
   * error sequences we'll ignore.
   */
  trap->end_sequence = XNextRequest (display->xdisplay);

  /* remove the Xlib callback */
  _gdk_x11_error_handler_pop ();

  /* we may already be outdated */
  delete_outdated_error_traps (display);

  return result;
}

void
meta_error_trap_pop (MetaDisplay *display)
{
  meta_error_trap_pop_internal (display, FALSE);
}

int
meta_error_trap_pop_with_return  (MetaDisplay *display)
{
  return meta_error_trap_pop_internal (display, TRUE);
}
