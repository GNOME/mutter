/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 *
 * Authors:
 *   Neil Roberts <neil@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-types.h"
#include "cogl/cogl-snippet-private.h"
#include "cogl/cogl-util.h"

G_DEFINE_FINAL_TYPE (CoglSnippet, cogl_snippet, G_TYPE_OBJECT);


static void
cogl_snippet_dispose (GObject *object)
{
  CoglSnippet *snippet = COGL_SNIPPET (object);

  g_free (snippet->declarations);
  g_free (snippet->pre);
  g_free (snippet->replace);
  g_free (snippet->post);

  G_OBJECT_CLASS (cogl_snippet_parent_class)->dispose (object);
}

static void
cogl_snippet_init (CoglSnippet *display)
{
}

static void
cogl_snippet_class_init (CoglSnippetClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_snippet_dispose;
}


CoglSnippet *
cogl_snippet_new (CoglSnippetHook hook,
                  const char *declarations,
                  const char *post)
{
  CoglSnippet *snippet = g_object_new (COGL_TYPE_SNIPPET, NULL);

  snippet->hook = hook;

  cogl_snippet_set_declarations (snippet, declarations);
  cogl_snippet_set_post (snippet, post);

  return snippet;
}

CoglSnippetHook
cogl_snippet_get_hook (CoglSnippet *snippet)
{
  g_return_val_if_fail (COGL_IS_SNIPPET (snippet), 0);

  return snippet->hook;
}

static gboolean
_cogl_snippet_modify (CoglSnippet *snippet)
{
  if (snippet->immutable)
    {
      g_warning ("A CoglSnippet should not be modified once it has been "
                 "attached to a pipeline. Any modifications after that point "
                 "will be ignored.");

      return FALSE;
    }

  return TRUE;
}

void
cogl_snippet_set_declarations (CoglSnippet *snippet,
                               const char *declarations)
{
  g_return_if_fail (COGL_IS_SNIPPET (snippet));

  if (!_cogl_snippet_modify (snippet))
    return;

  g_free (snippet->declarations);
  snippet->declarations = declarations ? g_strdup (declarations) : NULL;
}

const char *
cogl_snippet_get_declarations (CoglSnippet *snippet)
{
  g_return_val_if_fail (COGL_IS_SNIPPET (snippet), NULL);

  return snippet->declarations;
}

void
cogl_snippet_set_pre (CoglSnippet *snippet,
                      const char *pre)
{
  g_return_if_fail (COGL_IS_SNIPPET (snippet));

  if (!_cogl_snippet_modify (snippet))
    return;

  g_free (snippet->pre);
  snippet->pre = pre ? g_strdup (pre) : NULL;
}

const char *
cogl_snippet_get_pre (CoglSnippet *snippet)
{
  g_return_val_if_fail (COGL_IS_SNIPPET (snippet), NULL);

  return snippet->pre;
}

void
cogl_snippet_set_replace (CoglSnippet *snippet,
                          const char *replace)
{
  g_return_if_fail (COGL_IS_SNIPPET (snippet));

  if (!_cogl_snippet_modify (snippet))
    return;

  g_free (snippet->replace);
  snippet->replace = replace ? g_strdup (replace) : NULL;
}

const char *
cogl_snippet_get_replace (CoglSnippet *snippet)
{
  g_return_val_if_fail (COGL_IS_SNIPPET (snippet), NULL);

  return snippet->replace;
}

void
cogl_snippet_set_post (CoglSnippet *snippet,
                       const char *post)
{
  g_return_if_fail (COGL_IS_SNIPPET (snippet));

  if (!_cogl_snippet_modify (snippet))
    return;

  g_free (snippet->post);
  snippet->post = post ? g_strdup (post) : NULL;
}

const char *
cogl_snippet_get_post (CoglSnippet *snippet)
{
  g_return_val_if_fail (COGL_IS_SNIPPET (snippet), NULL);

  return snippet->post;
}

void
_cogl_snippet_make_immutable (CoglSnippet *snippet)
{
  snippet->immutable = TRUE;
}

void
cogl_snippet_set_capability (CoglSnippet  *snippet,
                             GQuark        domain,
                             unsigned int  capability)
{
  g_return_if_fail (!snippet->capability_domain);

  snippet->capability_domain = domain;
  snippet->capability = capability;
}

gboolean
cogl_snippet_get_capability (CoglSnippet  *snippet,
                             GQuark       *domain,
                             unsigned int *capability)
{
  if (snippet->capability_domain)
    {
      *domain = snippet->capability_domain;
      *capability = snippet->capability;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}
