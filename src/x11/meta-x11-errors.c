/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2001 Havoc Pennington, error trapping inspired by GDK
 * code copyrighted by the GTK team.
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
 * Errors:
 *
 * Mutter X error handling
 */

#include "config.h"

#include "meta/meta-x11-errors.h"

#include <errno.h>
#include <stdlib.h>
#include <X11/Xlibint.h>

#include "x11/meta-x11-display-private.h"

/* compare X sequence numbers handling wraparound */
#define SEQUENCE_COMPARE(a,op,b) (((long) (a) - (long) (b)) op 0)

typedef struct _MetaErrorTrap
{
  /* Next sequence when trap was pushed, i.e. first sequence to
   * ignore
   */
  unsigned long start_sequence;

  /* Next sequence when trap was popped, i.e. first sequence
   * to not ignore. 0 if trap is still active.
   */
  unsigned long end_sequence;

  /* Most recent error code within the sequence */
  int error_code;
} MetaErrorTrap;

/* Previously existing error handler */
typedef int (* MetaXErrorHandler) (Display *, XErrorEvent *);
static MetaXErrorHandler old_error_handler = NULL;
/* number of times we've pushed the error handler */
static int error_handler_push_count = 0;
static MetaX11Display *error_x11_display = NULL;

/* look up the extension name for a given major opcode.  grubs around in
 * xlib to do it since a) it’s already cached there b) XQueryExtension
 * emits protocol so we can’t use it in an error handler.
 */
static const char *
decode_request_code (Display *dpy,
                     int      code)
{
  _XExtension *ext;

  if (code < 128)
    return "core protocol";

  for (ext = dpy->ext_procs; ext; ext = ext->next)
    {
      if (ext->codes.major_opcode == code)
        return ext->name;
    }

  return "unknown";
}

static void
display_error_event (MetaX11Display *x11_display,
                     XErrorEvent    *error)
{
  GList *l;
  gboolean ignore = FALSE;

  for (l = x11_display->error_traps; l; l = l->next)
    {
      MetaErrorTrap *trap;

      trap = l->data;

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
      char buf[64];

      XGetErrorText (x11_display->xdisplay, error->error_code, buf, 63);

      g_error ("Received an X Window System error.\n"
               "This probably reflects a bug in the program.\n"
               "The error was '%s'.\n"
               "  (Details: serial %ld error_code %d request_code %d (%s) minor_code %d)\n"
               "  (Note to programmers: normally, X errors are reported asynchronously;\n"
               "   that is, you will receive the error a while after causing it.\n"
               "   To debug your program, run it with the MUTTER_SYNC environment\n"
               "   variable to change this behavior. You can then get a meaningful\n"
               "   backtrace from your debugger if you break on the meta_x_error() function.)",
               buf,
               error->serial,
               error->error_code,
               error->request_code,
               decode_request_code (x11_display->xdisplay, error->request_code),
               error->minor_code);
    }
}

static int
meta_x_error (Display     *xdisplay,
              XErrorEvent *error)
{
  if (error->error_code)
    display_error_event (error_x11_display, error);

  return 0;
}

static void
error_handler_push (void)
{
  MetaXErrorHandler previous_handler;

  previous_handler = XSetErrorHandler (meta_x_error);

  if (error_handler_push_count > 0)
    {
      if (previous_handler != meta_x_error)
        g_warning ("XSetErrorHandler() called with a Mutter X11 error trap pushed. Don't do that.");
    }
  else
    {
      old_error_handler = previous_handler;
    }

  error_handler_push_count += 1;
}

static void
error_handler_pop (void)
{
  g_return_if_fail (error_handler_push_count > 0);

  error_handler_push_count -= 1;

  if (error_handler_push_count == 0)
    {
      XSetErrorHandler (old_error_handler);
      old_error_handler = NULL;
    }
}

static void
delete_outdated_error_traps (MetaX11Display *x11_display)
{
  GList *l;
  unsigned long processed_sequence;

  processed_sequence = XLastKnownRequestProcessed (x11_display->xdisplay);
  l = x11_display->error_traps;

  while (l != NULL)
    {
      MetaErrorTrap *trap = l->data;

      if (trap->end_sequence != 0 &&
          SEQUENCE_COMPARE (trap->end_sequence, <=, processed_sequence))
        {
          GList *link = l;

          l = l->next;
          x11_display->error_traps =
            g_list_delete_link (x11_display->error_traps, link);
          g_free (trap);
        }
      else
        {
          l = l->next;
        }
    }
}

void
meta_x11_display_init_error_traps (MetaX11Display *x11_display)
{
  g_assert (error_x11_display == NULL);
  error_x11_display = x11_display;
  XSetErrorHandler (meta_x_error);
}

void
meta_x11_display_destroy_error_traps (MetaX11Display *x11_display)
{
  if (error_x11_display == NULL)
    return;

  g_assert (error_x11_display == x11_display);
  g_clear_list (&x11_display->error_traps, g_free);
  error_x11_display = NULL;
  XSetErrorHandler (NULL);
}

void
meta_x11_error_trap_push (MetaX11Display *x11_display)
{
  MetaErrorTrap *trap;

  delete_outdated_error_traps (x11_display);

  /* set up the Xlib callback to tell us about errors */
  error_handler_push ();

  trap = g_new0 (MetaErrorTrap, 1);
  trap->start_sequence = XNextRequest (x11_display->xdisplay);
  trap->error_code = Success;

  x11_display->error_traps =
    g_list_prepend (x11_display->error_traps, trap);
}

static int
meta_x11_error_trap_pop_internal (MetaX11Display *x11_display,
                                  gboolean        need_code)
{
  MetaErrorTrap *trap = NULL;
  GList *l;
  int result;

  g_return_val_if_fail (x11_display->error_traps != NULL, Success);

  /* Find the first trap that hasn't been popped already */
  for (l = x11_display->error_traps; l; l = l->next)
    {
      trap = l->data;

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
      unsigned long processed_sequence, next_sequence;

      next_sequence = XNextRequest (x11_display->xdisplay);
      processed_sequence = XLastKnownRequestProcessed (x11_display->xdisplay);

      /* If our last request was already processed, there is no point
       * in syncing. i.e. if last request was a round trip (or even if
       * we got an event with the serial of a non-round-trip)
       */
      if ((next_sequence - 1) != processed_sequence)
        {
          XSync (x11_display->xdisplay, False);
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
  trap->end_sequence = XNextRequest (x11_display->xdisplay);

  /* remove the Xlib callback */
  error_handler_pop ();

  /* we may already be outdated */
  delete_outdated_error_traps (x11_display);

  return result;
}

void
meta_x11_error_trap_pop (MetaX11Display *x11_display)
{
  meta_x11_error_trap_pop_internal (x11_display, FALSE);
}

int
meta_x11_error_trap_pop_with_return (MetaX11Display *x11_display)
{
  return meta_x11_error_trap_pop_internal (x11_display, TRUE);
}
