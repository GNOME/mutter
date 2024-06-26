/*
 * Copyright © 2002 Keith Packard
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * A slightly modified version of XCursor used with Wayland only builds.
 */
#pragma once

#ifdef HAVE_X11_CLIENT
#include <X11/Xcursor/Xcursor.h>
#else
typedef unsigned int XcursorPixel;

typedef struct _XcursorImage {
  unsigned int version; /* version of the image data */
  unsigned int size; /* nominal size for matching */
  unsigned int width; /* actual width */
  unsigned int height; /* actual height */
  unsigned int xhot; /* hot spot x (must be inside image) */
  unsigned int yhot; /* hot spot y (must be inside image) */
  unsigned int delay; /* animation delay to next frame (ms) */
  XcursorPixel *pixels; /* pointer to pixels */
} XcursorImage;

typedef struct _XcursorImages {
  int nimage;  /* number of images */
  XcursorImage **images; /* array of XcursorImage pointers */
  char *name; /* name used to load images */
} XcursorImages;
#endif


void xcursor_images_destroy (XcursorImages *images);

XcursorImages * xcursor_images_create (int size);

XcursorImages * xcursor_library_load_images (const char *library,
                                             const char *theme,
                                             int         size);

XcursorImage * xcursor_image_create (int width,
                                     int height);
