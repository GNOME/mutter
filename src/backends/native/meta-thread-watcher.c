/*
 * Copyright (C) 2023 Red Hat
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

#include "backends/native/meta-thread-watcher.h"

#include "clutter/clutter.h"
#include <fcntl.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "clutter/clutter-private.h"
#include "core/util-private.h"

/* There's a watchdog timer that if left to its own devices will fire after
 * priv->interval_ms milliseconds.
 *
 * There's a GLib main loop timeout that runs just before the watchdog timer fires
 * to reset the timer to run later.
 *
 * If the main loop is ever blocked the main loop timeout won't run and the timer
 * wont get reset. In this way we can tell if the thread with the main loop is stalled.
 *
 * WATCH_INTERVAL_PHASE_OFFSET_MS is how many milliseconds before the watchdog timer
 * is supposed to fire that the main loop timeout is supposed to reset the timer.
 *
 * It just needs to be long enough for timer_settime to be called, but there's
 * no real disadvantage to making it longer, so long as it's less the thread rlimit.
 *
 * It's currently set somewhat arbitrarily at 16ms (approximately one rendererd frame
 * on most machines)
 */
#define WATCH_INTERVAL_PHASE_OFFSET_MS ms (16)

static gboolean backtrace_printed = FALSE;
static struct timespec check_ins[2];
static ssize_t check_in_index = -1;

enum
{
  THREAD_STALLED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaThreadWatcher
{
  GObject parent_instance;
};

typedef struct _MetaThreadWatcherPrivate
{
  int fds[2];
  int64_t interval_ms;
  timer_t *timer;
  guint notification_watch_id;
  guint checker_watch_id;
  GMainContext *context;
  GSource *source;
  pid_t thread_id;
} MetaThreadWatcherPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaThreadWatcher, meta_thread_watcher, G_TYPE_OBJECT)

static void
meta_thread_watcher_constructed (GObject *object)
{
  MetaThreadWatcher *watcher = META_THREAD_WATCHER (object);
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);

  priv->timer = NULL;
  priv->fds[0] = -1;
  priv->fds[1] = -1;

  G_OBJECT_CLASS (meta_thread_watcher_parent_class)->constructed (object);
}

static void
close_fds (MetaThreadWatcher *watcher)
{
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);

  g_clear_fd (&priv->fds[0], NULL);
  g_clear_fd (&priv->fds[1], NULL);
}

static void
notify_watched_thread (int fd)
{
  ssize_t bytes_written = 0;

  do
    {
      bytes_written = write (fd, "", 1);
    }
  while (bytes_written < 0 && errno == EINTR);
}

static void
clear_notifications (MetaThreadWatcher *watcher)
{
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);
  ssize_t bytes_read;
  char buf[64];

  do
    {
      bytes_read = read (priv->fds[0], buf, sizeof (buf));
    }
  while (bytes_read > 0 || (bytes_read < 0 && errno == EINTR));
}

static void
yield (void)
{
  struct pollfd sleep_pollfd = { -1, 0, 0 };
  poll (&sleep_pollfd, 0, 1);
}

static void
on_xcpu_signal (int        signal,
                siginfo_t *signal_data,
                void      *context)
{
  int fd;

  /* If we're getting the XCPU signal that means the realtime thread is blocked and
   * mutter is at risk of being killed by the kernel. We can placate the kernel by
   * sleeping for a millisecond or so. That should buy us another ~200ms to tear
   * down the realtime thread and get out of the dangerzone.
   */
  yield ();

  /* If we're here, there's a bug somewhere, so send backtraces to the journal.
   */
  if (!backtrace_printed)
    {
      backtrace_printed = TRUE;
      const char *message = "Hang in realtime thread detected by timer signal! Backtrace:\n";
      write (STDERR_FILENO, message, strlen (message));
      meta_print_backtrace ();
    }

  if (signal_data->si_pid != 0 || signal_data->si_code != SI_TIMER)
    return;

  fd = signal_data->si_value.sival_int;
  notify_watched_thread (fd);
}

static void
meta_thread_watcher_finalize (GObject *object)
{
  MetaThreadWatcher *watcher = META_THREAD_WATCHER (object);

  meta_thread_watcher_stop (watcher);
  G_OBJECT_CLASS (meta_thread_watcher_parent_class)->finalize (object);
}

static void
meta_thread_watcher_class_init (MetaThreadWatcherClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  struct sigaction signal_request;
  int ret;

  object_class->constructed = meta_thread_watcher_constructed;
  object_class->finalize = meta_thread_watcher_finalize;

  signals[THREAD_STALLED] =
    g_signal_new ("thread-stalled",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signal_request.sa_flags = SA_SIGINFO;
  signal_request.sa_sigaction = on_xcpu_signal;
  sigemptyset (&signal_request.sa_mask);
  ret = sigaction (SIGXCPU, &signal_request, NULL);

  if (ret == -1)
    g_warning ("Failed to listen for SIGXCPU signal: %m");
}

static void
meta_thread_watcher_init (MetaThreadWatcher *thread_watcher)
{
}

MetaThreadWatcher *
meta_thread_watcher_new (void)
{
  MetaThreadWatcher *watcher;

  watcher = g_object_new (META_TYPE_THREAD_WATCHER, NULL);

  return watcher;
}

static gboolean
on_reset_timer (MetaThreadWatcher *watcher)
{
  g_autoptr (GError) error = NULL;
  gboolean was_reset;

  if (!meta_thread_watcher_is_started (watcher))
    return G_SOURCE_REMOVE;

  was_reset = meta_thread_watcher_reset (watcher, &error);

  if (!was_reset)
    {
      g_warning ("Failed to reset real-time thread watchdog timer: %s",
                 error->message);
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

void
meta_thread_watcher_attach (MetaThreadWatcher *watcher,
                            GMainContext      *context)
{
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);
  g_return_if_fail (META_IS_THREAD_WATCHER (watcher));
  g_return_if_fail (priv->source == NULL);

  g_clear_pointer (&priv->context, g_main_context_unref);
  priv->context = g_main_context_ref (context);
}

void
meta_thread_watcher_detach (MetaThreadWatcher *watcher)
{
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);

  g_return_if_fail (META_IS_THREAD_WATCHER (watcher));

  g_clear_pointer (&priv->source, g_source_destroy);
  g_clear_pointer (&priv->context, g_main_context_unref);
}

static void
check_thread (MetaThreadWatcher *watcher)
{
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);
  struct timespec current_time;
  ssize_t last_check_in_index = check_in_index;
  size_t cpu_time_delta_ms;
  int ret;

  g_clear_handle_id (&priv->checker_watch_id, g_source_remove);

  if (!meta_thread_watcher_is_started (watcher))
    return;

  check_in_index = (check_in_index + 1) % G_N_ELEMENTS (check_ins);

  if (last_check_in_index >= 0)
    {
      static gboolean started_before;

      if (!started_before)
        {
          g_warning ("Starting main thread watchdog");
          started_before = TRUE;
        }
      ret = clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &current_time);

      if (ret == -1)
        {
          g_warning ("Failed to re-read current CPU time of process: %m");
          return;
        }

      cpu_time_delta_ms = us2ms (s2us (current_time.tv_sec - check_ins[last_check_in_index].tv_sec) + us ((current_time.tv_nsec - check_ins[last_check_in_index].tv_nsec) / 1000));

      if (cpu_time_delta_ms > ms (32))
        {
          if (!backtrace_printed)
            {
              backtrace_printed = TRUE;
              g_warning ("Hang in realtime thread detected by main thread! (%dms since last check-in). Backtrace:\n", (int) cpu_time_delta_ms);
              meta_print_backtrace ();
            }
          g_signal_emit (G_OBJECT (watcher), signals[THREAD_STALLED], 0);
          return;
        }
    }
  else
    {
      g_warning ("Beginning realtime thread watcher on main thread");
      clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &check_ins[check_in_index]);
      clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &check_ins[check_in_index + 1]);
    }

  priv->checker_watch_id = g_timeout_add_once (ms (8), (GSourceOnceFunc) check_thread, watcher);
}

static gboolean
on_thread_stalled (int                fd,
                   GIOCondition       condition,
                   MetaThreadWatcher *watcher)
{
  if (condition & G_IO_IN)
    clear_notifications (watcher);

  if (meta_thread_watcher_is_started (watcher))
    g_signal_emit (G_OBJECT (watcher), signals[THREAD_STALLED], 0);

  return G_SOURCE_REMOVE;
}

gboolean
meta_thread_watcher_is_started (MetaThreadWatcher *watcher)
{
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);

  g_return_val_if_fail (META_IS_THREAD_WATCHER (watcher), FALSE);

  return priv->timer != NULL;
}

static void
meta_thread_watcher_clear_source (MetaThreadWatcher *watcher)
{
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);
  priv->source = NULL;
}

gboolean
meta_thread_watcher_start (MetaThreadWatcher  *watcher,
                           int                 interval_us,
                           GError            **error)
{
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);
  GSource *source = NULL;
  sigevent_t timer_request;
  int ret;
  g_autofree timer_t *timer = NULL;

  g_return_val_if_fail (META_IS_THREAD_WATCHER (watcher), FALSE);
  g_return_val_if_fail (interval_us > ms2us (WATCH_INTERVAL_PHASE_OFFSET_MS), FALSE);
  g_return_val_if_fail (priv->context != NULL, FALSE);

  if (meta_thread_watcher_is_started (watcher))
    return TRUE;

  priv->interval_ms = us2ms (interval_us);
  priv->thread_id = gettid ();

  if (!g_unix_open_pipe (priv->fds,
                         FD_CLOEXEC | O_NONBLOCK,
                         error))
    return FALSE;

  if (priv->fds[0] == -1 || priv->fds[1] == -1)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (EBADF),
                   "Thread watcher could not create pipe");
      return FALSE;
    }

  timer_request.sigev_notify = SIGEV_THREAD_ID;
  timer_request.sigev_signo = SIGXCPU;
  timer_request.sigev_value.sival_int = priv->fds[1];
  timer_request._sigev_un._tid = priv->thread_id;
  timer = g_new0 (timer_t, 1);
  ret = timer_create (CLOCK_THREAD_CPUTIME_ID, &timer_request, timer);

  if (ret == -1)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to create unix timer: %s", g_strerror (errno));
      return FALSE;
    }

  priv->timer = g_steal_pointer (&timer);

  if (!meta_thread_watcher_reset (watcher, error))
    return FALSE;

  priv->checker_watch_id = g_timeout_add_once (ms (15000), (GSourceOnceFunc) check_thread, watcher);

  priv->notification_watch_id = g_unix_fd_add (priv->fds[0],
                                               G_IO_IN,
                                               (GUnixFDSourceFunc) on_thread_stalled,
                                               watcher);

  source = g_timeout_source_new (ms (16));
  g_source_set_name (source, "[mutter] Thread watcher");
  g_source_set_callback (source,
                         (GSourceFunc) on_reset_timer,
                         watcher,
                         (GDestroyNotify)
                         meta_thread_watcher_clear_source);
  g_source_attach (source, priv->context);
  g_source_unref (source);

  priv->source = source;

  return TRUE;
}

static void
free_timer (timer_t *timer)
{
  if (!timer)
    return;

  timer_delete (*timer);
}

gboolean
meta_thread_watcher_reset (MetaThreadWatcher  *watcher,
                           GError            **error)
{
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);
  struct itimerspec timer_interval;
  int ret;

  g_return_val_if_fail (META_IS_THREAD_WATCHER (watcher), FALSE);

  if (check_in_index >= 0)
    {
      ret = clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &check_ins[check_in_index]);

      if (ret == -1)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Failed to re-read current CPU time of process: %s", g_strerror (errno));
          meta_thread_watcher_stop (watcher);

          return FALSE;
        }
    }

  timer_interval.it_value.tv_sec = 0;
  timer_interval.it_value.tv_nsec = ms2ns (priv->interval_ms);
  timer_interval.it_interval.tv_sec = timer_interval.it_value.tv_sec;
  timer_interval.it_interval.tv_nsec = timer_interval.it_value.tv_nsec;

  ret = timer_settime (*priv->timer, 0, &timer_interval, NULL);

  if (ret == -1)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to arm unix timer: %s", g_strerror (errno));
      meta_thread_watcher_stop (watcher);

      return FALSE;
    }

  return TRUE;
}

void
meta_thread_watcher_stop (MetaThreadWatcher *watcher)
{
  MetaThreadWatcherPrivate *priv = meta_thread_watcher_get_instance_private (watcher);

  g_return_if_fail (META_IS_THREAD_WATCHER (watcher));

  if (!meta_thread_watcher_is_started (watcher))
    return;

  meta_thread_watcher_detach (watcher);

  g_clear_pointer (&priv->timer, free_timer);
  g_clear_handle_id (&priv->notification_watch_id, g_source_remove);
  g_clear_handle_id (&priv->checker_watch_id, g_source_remove);
  close_fds (watcher);
}
