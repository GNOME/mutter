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

#include "mtk/mtk-x11-errors.h"

#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <X11/Xlibint.h>

/* compare X sequence numbers handling wraparound */
#define SEQUENCE_COMPARE(a,op,b) (((long) (a) - (long) (b)) op 0)

typedef struct _MtkErrorTrap
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
} MtkErrorTrap;

/* Previously existing error handler */
typedef int (* MtkXErrorHandler) (Display *, XErrorEvent *);
static MtkXErrorHandler old_error_handler = NULL;
/* number of times we've pushed the error handler */
static int error_handler_push_count = 0;
static GHashTable *display_error_traps = NULL;
static int init_count = 0;

/* look up the extension name for a given major opcode.  grubs around in
 * xlib to do it since a) it’s already cached there b) XQueryExtension
 * emits protocol so we can’t use it in an error handler.
 */
static const char *
decode_request_code (Display *xdisplay,
                     int      code)
{
  _XExtension *ext;

  if (code < 128)
    return "core protocol";

  for (ext = xdisplay->ext_procs; ext; ext = ext->next)
    {
      if (ext->codes.major_opcode == code)
        return ext->name;
    }

  return "unknown";
}

static void
display_error_event (Display     *xdisplay,
                     XErrorEvent *error)
{
  GList *l;
  gboolean ignore = FALSE;
  GList *error_traps;

  error_traps = g_hash_table_lookup (display_error_traps, xdisplay);

  for (l = error_traps; l; l = l->next)
    {
      MtkErrorTrap *trap;

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

      XGetErrorText (xdisplay, error->error_code, buf, 63);

      g_error ("Received an X Window System error.\n"
               "This probably reflects a bug in the program.\n"
               "The error was '%s'.\n"
               "  (Details: serial %ld error_code %d request_code %d (%s) minor_code %d)\n"
               "  (Note to programmers: normally, X errors are reported asynchronously;\n"
               "   that is, you will receive the error a while after causing it.\n"
               "   To debug your program, run it with the MUTTER_SYNC environment\n"
               "   variable to change this behavior. You can then get a meaningful\n"
               "   backtrace from your debugger if you break on the mtk_x_error() function.)",
               buf,
               error->serial,
               error->error_code,
               error->request_code,
               decode_request_code (xdisplay, error->request_code),
               error->minor_code);
    }
}

static int
mtk_x_error (Display     *xdisplay,
             XErrorEvent *error)
{
  if (error->error_code)
    display_error_event (xdisplay, error);

  return 0;
}

static void
error_handler_push (void)
{
  MtkXErrorHandler previous_handler;

  previous_handler = XSetErrorHandler (mtk_x_error);

  if (error_handler_push_count > 0)
    {
      if (previous_handler != mtk_x_error)
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
delete_outdated_error_traps (Display *xdisplay)
{
  GList *l, *error_traps;
  unsigned long processed_sequence;

  processed_sequence = XLastKnownRequestProcessed (xdisplay);
  l = error_traps = g_hash_table_lookup (display_error_traps, xdisplay);
  g_hash_table_steal (display_error_traps, xdisplay);

  while (l != NULL)
    {
      MtkErrorTrap *trap = l->data;

      if (trap->end_sequence != 0 &&
          SEQUENCE_COMPARE (trap->end_sequence, <=, processed_sequence))
        {
          GList *link = l;

          l = l->next;
          error_traps = g_list_delete_link (error_traps, link);
          g_free (trap);
        }
      else
        {
          l = l->next;
        }
    }

  g_hash_table_insert (display_error_traps, xdisplay, error_traps);
}

static void
free_trap_list (gpointer data)
{
  g_list_free_full (data, g_free);
}

/**
 * mtk_x11_errors_init: (skip):
 */
void
mtk_x11_errors_init (void)
{
  if (init_count == 0)
    {
      XSetErrorHandler (mtk_x_error);
      display_error_traps =
        g_hash_table_new_full (NULL, NULL, NULL, free_trap_list);
    }

  init_count++;
}

/**
 * mtk_x11_errors_destroy: (skip):
 */
void
mtk_x11_errors_deinit (void)
{
  init_count--;
  g_assert (init_count >= 0);

  if (init_count == 0)
    {
      g_clear_pointer (&display_error_traps, g_hash_table_unref);
      XSetErrorHandler (NULL);
    }
}

/**
 * mtk_x11_error_trap_push: (skip):
 */
void
mtk_x11_error_trap_push (Display *xdisplay)
{
  MtkErrorTrap *trap;
  GList *error_traps;

  delete_outdated_error_traps (xdisplay);

  /* set up the Xlib callback to tell us about errors */
  error_handler_push ();

  trap = g_new0 (MtkErrorTrap, 1);
  trap->start_sequence = XNextRequest (xdisplay);
  trap->error_code = Success;

  error_traps = g_hash_table_lookup (display_error_traps, xdisplay);
  g_hash_table_steal (display_error_traps, xdisplay);
  error_traps = g_list_prepend (error_traps, trap);
  g_hash_table_insert (display_error_traps, xdisplay, error_traps);
}

static int
mtk_x11_error_trap_pop_internal (Display  *xdisplay,
                                 gboolean  need_code)
{
  MtkErrorTrap *trap = NULL;
  GList *l, *error_traps;
  int result;

  error_traps = g_hash_table_lookup (display_error_traps, xdisplay);

  g_return_val_if_fail (error_traps != NULL, Success);

  /* Find the first trap that hasn't been popped already */
  for (l = error_traps; l; l = l->next)
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

      next_sequence = XNextRequest (xdisplay);
      processed_sequence = XLastKnownRequestProcessed (xdisplay);

      /* If our last request was already processed, there is no point
       * in syncing. i.e. if last request was a round trip (or even if
       * we got an event with the serial of a non-round-trip)
       */
      if ((next_sequence - 1) != processed_sequence)
        {
          XSync (xdisplay, False);
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
  trap->end_sequence = XNextRequest (xdisplay);

  /* remove the Xlib callback */
  error_handler_pop ();

  /* we may already be outdated */
  delete_outdated_error_traps (xdisplay);

  return result;
}

/**
 * mtk_x11_error_trap_pop: (skip):
 */
void
mtk_x11_error_trap_pop (Display *xdisplay)
{
  mtk_x11_error_trap_pop_internal (xdisplay, FALSE);
}

/**
 * mtk_x11_error_trap_pop_with_return: (skip):
 */
int
mtk_x11_error_trap_pop_with_return (Display *xdisplay)
{
  return mtk_x11_error_trap_pop_internal (xdisplay, TRUE);
}
