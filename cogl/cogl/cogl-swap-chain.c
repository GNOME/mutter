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
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-swap-chain-private.h"
#include "cogl/cogl-swap-chain.h"

G_DEFINE_TYPE (CoglSwapChain, cogl_swap_chain, G_TYPE_OBJECT);

static void
cogl_swap_chain_init (CoglSwapChain *swap_chain)
{
}

static void
cogl_swap_chain_class_init (CoglSwapChainClass *class)
{
}

CoglSwapChain *
cogl_swap_chain_new (void)
{
  CoglSwapChain *swap_chain = g_object_new (COGL_TYPE_SWAP_CHAIN, NULL);

  swap_chain->length = -1; /* no preference */

  return swap_chain;
}

void
cogl_swap_chain_set_length (CoglSwapChain *swap_chain,
                            int length)
{
  swap_chain->length = length;
}
