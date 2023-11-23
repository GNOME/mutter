/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2005 Elijah Newren
 * Copyright (C) 2020 Red Hat Inc.
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

#include "compositor/meta-later-private.h"

#include "cogl/cogl.h"
#include "compositor/compositor-private.h"
#include "core/display-private.h"
#include "meta/meta-later.h"

typedef struct _MetaLater
{
  MetaLaters *laters;

  unsigned int id;
  unsigned int ref_count;
  MetaLaterType when;

  GSourceFunc func;
  gpointer user_data;
  GDestroyNotify destroy_notify;

  guint source_id;
  gboolean run_once;
} MetaLater;

#define META_LATER_N_TYPES (META_LATER_IDLE + 1)

struct _MetaLaters
{
  GObject parent;

  MetaCompositor *compositor;

  unsigned int last_later_id;

  GSList *laters[META_LATER_N_TYPES];

  gulong before_update_handler_id;
};

G_DEFINE_TYPE (MetaLaters, meta_laters, G_TYPE_OBJECT)

static MetaLater *
meta_later_ref (MetaLater *later)
{
  later->ref_count++;
  return later;
}

static void
meta_later_unref (MetaLater *later)
{
  if (--later->ref_count == 0)
    {
      if (later->destroy_notify)
        {
          later->destroy_notify (later->user_data);
          later->destroy_notify = NULL;
        }

      g_free (later);
    }
}

static void
meta_later_destroy (MetaLater *later)
{
  g_clear_handle_id (&later->source_id, g_source_remove);
  later->func = NULL;
  meta_later_unref (later);
}

#ifdef HAVE_PROFILER
static const char *
later_type_to_string (MetaLaterType when)
{
  switch (when)
    {
    case META_LATER_RESIZE:
      return "resize";
    case META_LATER_CALC_SHOWING:
      return "calc-showing";
    case META_LATER_CHECK_FULLSCREEN:
      return "check-fullscreen";
    case META_LATER_SYNC_STACK:
      return "sync-stack";
    case META_LATER_BEFORE_REDRAW:
      return "before-redraw";
    case META_LATER_IDLE:
      return "idle";
    }

  return "unknown";
}
#endif

static gboolean
meta_later_invoke (MetaLater *later)
{
  COGL_TRACE_BEGIN_SCOPED (later, "Meta::Later::invoke()");
  COGL_TRACE_DESCRIBE (later, later_type_to_string (later->when));

  return later->func (later->user_data);
}

static gboolean
remove_later_from_list (unsigned int   later_id,
                        GSList       **laters_list)
{
  GSList *l;

  for (l = *laters_list; l; l = l->next)
    {
      MetaLater *later = l->data;

      if (later->id == later_id)
        {
          *laters_list = g_slist_delete_link (*laters_list, l);
          meta_later_destroy (later);
          return TRUE;
        }
    }

  return FALSE;
}

static void
run_repaint_laters (GSList **laters_list)
{
  g_autoptr (GSList) laters_copy = NULL;
  GSList *l;

  for (l = *laters_list; l; l = l->next)
    {
      MetaLater *later = l->data;

      if (!later->source_id ||
          (later->when <= META_LATER_BEFORE_REDRAW && !later->run_once))
        laters_copy = g_slist_prepend (laters_copy, meta_later_ref (later));
    }
  laters_copy = g_slist_reverse (laters_copy);

  for (l = laters_copy; l; l = l->next)
    {
      MetaLater *later = l->data;

      if (!later->func)
        remove_later_from_list (later->id, laters_list);
      else if (!meta_later_invoke (later))
        remove_later_from_list (later->id, laters_list);

      meta_later_unref (later);
    }
}

static void
on_before_update (ClutterStage     *stage,
                  ClutterStageView *stage_view,
                  ClutterFrame     *frame,
                  MetaLaters       *laters)
{
  unsigned int i;
  GSList *l;
  gboolean needs_schedule_update = FALSE;

  for (i = 0; i < G_N_ELEMENTS (laters->laters); i++)
    run_repaint_laters (&laters->laters[i]);

  for (i = 0; i < G_N_ELEMENTS (laters->laters); i++)
    {
      for (l = laters->laters[i]; l; l = l->next)
        {
          MetaLater *later = l->data;

          if (!later->source_id)
            needs_schedule_update = TRUE;
        }
    }

  if (needs_schedule_update)
    clutter_stage_schedule_update (stage);
}

static gboolean
invoke_later_idle (gpointer data)
{
  MetaLater *later = data;

  if (!later->func (later->user_data))
    {
      meta_laters_remove (later->laters, later->id);
      return FALSE;
    }
  else
    {
      later->run_once = TRUE;
      return TRUE;
    }
}

static void
meta_laters_finalize (GObject *object)
{
  MetaLaters *laters = META_LATERS (object);

  ClutterStage *stage = meta_compositor_get_stage (laters->compositor);
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (laters->laters); i++)
    g_slist_free_full (laters->laters[i], (GDestroyNotify) meta_later_unref);

  g_clear_signal_handler (&laters->before_update_handler_id, stage);

  G_OBJECT_CLASS (meta_laters_parent_class)->finalize (object);
}

static void
meta_laters_class_init (MetaLatersClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_laters_finalize;
}

static void
meta_laters_init (MetaLaters *laters)
{
}

/**
 * meta_laters_add:
 * @laters: a #MetaLaters
 * @when: enumeration value determining the phase at which to run the callback
 * @func: callback to run later
 * @user_data: data to pass to the callback
 * @notify: function to call to destroy @data when it is no longer in use, or %NULL
 *
 * Sets up a callback  to be called at some later time. @when determines the
 * particular later occasion at which it is called. This is much like g_idle_add(),
 * except that the functions interact properly with clutter event handling.
 * If a "later" function is added from a clutter event handler, and is supposed
 * to be run before the stage is redrawn, it will be run before that redraw
 * of the stage, not the next one.
 *
 * Return value: an integer ID (guaranteed to be non-zero) that can be used
 *  to cancel the callback and prevent it from being run.
 */
unsigned int
meta_laters_add (MetaLaters     *laters,
                 MetaLaterType   when,
                 GSourceFunc     func,
                 gpointer        user_data,
                 GDestroyNotify  notify)
{
  ClutterStage *stage = meta_compositor_get_stage (laters->compositor);
  MetaLater *later = g_new0 (MetaLater, 1);

  later->id = ++laters->last_later_id;
  later->laters = laters;
  later->ref_count = 1;
  later->when = when;
  later->func = func;
  later->user_data = user_data;
  later->destroy_notify = notify;

  laters->laters[when] = g_slist_prepend (laters->laters[when], later);

  switch (when)
    {
    case META_LATER_RESIZE:
      later->source_id = g_idle_add_full (META_PRIORITY_RESIZE,
                                          invoke_later_idle,
                                          later, NULL);
      g_source_set_name_by_id (later->source_id, "[mutter] invoke_later_idle");
      clutter_stage_schedule_update (stage);
      break;
    case META_LATER_CALC_SHOWING:
    case META_LATER_CHECK_FULLSCREEN:
    case META_LATER_SYNC_STACK:
    case META_LATER_BEFORE_REDRAW:
      clutter_stage_schedule_update (stage);
      break;
    case META_LATER_IDLE:
      later->source_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                                          invoke_later_idle,
                                          later, NULL);
      g_source_set_name_by_id (later->source_id, "[mutter] invoke_later_idle");
      break;
    }

  return later->id;
}

/**
 * meta_laters_remove:
 * @laters: a #MetaLaters
 * @later_id: the integer ID returned from meta_later_add()
 *
 * Removes a callback added with meta_later_add()
 */
void
meta_laters_remove (MetaLaters   *laters,
                    unsigned int  later_id)
{
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (laters->laters); i++)
    {
      if (remove_later_from_list (later_id, &laters->laters[i]))
        return;
    }
}

MetaLaters *
meta_laters_new (MetaCompositor *compositor)
{
  ClutterStage *stage = meta_compositor_get_stage (compositor);
  MetaLaters *laters;

  laters = g_object_new (META_TYPE_LATERS, NULL);
  laters->compositor = compositor;

  laters->before_update_handler_id =
    g_signal_connect (stage, "before-update",
                      G_CALLBACK (on_before_update),
                      laters);

  return laters;
}
