/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012,2013 Intel Corporation.
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
 */

#pragma once

#include "cogl/cogl-list.h"
#include "cogl/cogl-macros.h"

/*
 * This implements a list of callbacks that can be used a bit like
 * signals in GObject, but that don't have any marshalling overhead.
 *
 * The idea is that any Cogl code that wants to provide a callback
 * point will provide api to add a callback for that particular point.
 * The function can take a function pointer with the correct
 * signature. Internally the Cogl code can use _cogl_closure_disconnect
 *
 * In the future we could consider exposing the CoglClosure type which
 * would allow applications to use _cogl_closure_disconnect() directly
 * so we don't need to expose new disconnect apis for each callback
 * point.
 */

typedef struct _CoglClosure
{
  CoglList link;

  void *function;
  void *user_data;
} CoglClosure;

/*
 * _cogl_closure_disconnect:
 * @closure: A closure connected to a Cogl closure list
 *
 * Removes the given closure from the callback list it is connected to
 * and destroys it. If the closure was created with a destroy function
 * then it will be invoked. */
void
_cogl_closure_disconnect (CoglClosure *closure);

