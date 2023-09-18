#include <clutter/clutter.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

static GQuark pixbuf_key = 0;

static inline ClutterActor *
clutter_test_utils_create_texture_from_file (const char  *filename,
                                             GError     **error)
{
  g_autoptr (ClutterContent) image = NULL;
  g_autoptr (GdkPixbuf) pixbuf = NULL;

  pixbuf = gdk_pixbuf_new_from_file (filename, error);
  if (!pixbuf)
    return NULL;

  image = clutter_image_new ();
  if (!clutter_image_set_data (CLUTTER_IMAGE (image),
                               gdk_pixbuf_get_pixels (pixbuf),
                               gdk_pixbuf_get_has_alpha (pixbuf)
                               ? COGL_PIXEL_FORMAT_RGBA_8888
                               : COGL_PIXEL_FORMAT_RGB_888,
                               gdk_pixbuf_get_width (pixbuf),
                               gdk_pixbuf_get_height (pixbuf),
                               gdk_pixbuf_get_rowstride (pixbuf),
                               error))
    return NULL;

  return g_object_new (CLUTTER_TYPE_ACTOR,
                       "content", image,
                       NULL);
}

static inline CoglBitmap *
clutter_test_create_bitmap_from_file (CoglContext  *ctx,
                                      const char   *filename,
                                      GError      **error)
{
  pixbuf_key = g_quark_from_static_string ("-cogl-bitmap-pixbuf-key");
  GdkPixbuf *pixbuf;
  gboolean has_alpha;
  GdkColorspace color_space;
  CoglPixelFormat pixel_format;
  int width;
  int height;
  int rowstride;
  int bits_per_sample;
  int n_channels;
  CoglBitmap *bmp;
  GError *glib_error = NULL;

  /* Load from file using GdkPixbuf */
  pixbuf = gdk_pixbuf_new_from_file (filename, &glib_error);
  if (pixbuf == NULL)
    {
      g_propagate_error (error, glib_error);
      return FALSE;
    }

  /* Get pixbuf properties */
  has_alpha = gdk_pixbuf_get_has_alpha (pixbuf);
  color_space = gdk_pixbuf_get_colorspace (pixbuf);
  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  bits_per_sample = gdk_pixbuf_get_bits_per_sample (pixbuf);
  n_channels = gdk_pixbuf_get_n_channels (pixbuf);

  /* According to current docs this should be true and so
   * the translation to cogl pixel format below valid */
  g_assert (bits_per_sample == 8);

  if (has_alpha)
    g_assert (n_channels == 4);
  else
    g_assert (n_channels == 3);

  /* Translate to cogl pixel format */
  switch (color_space)
    {
    case GDK_COLORSPACE_RGB:
      /* The only format supported by GdkPixbuf so far */
      pixel_format = has_alpha ?
                     COGL_PIXEL_FORMAT_RGBA_8888 :
                     COGL_PIXEL_FORMAT_RGB_888;
      break;

    default:
      /* Ouch, spec changed! */
      g_object_unref (pixbuf);
      return FALSE;
    }

  /* We just use the data directly from the pixbuf so that we don't
     have to copy to a separate buffer. Note that Cogl is expected not
     to read past the end of bpp*width on the last row even if the
     rowstride is much larger so we don't need to worry about
     GdkPixbuf's semantics that it may under-allocate the buffer. */
  bmp = cogl_bitmap_new_for_data (ctx,
                                  width,
                                  height,
                                  pixel_format,
                                  rowstride,
                                  gdk_pixbuf_get_pixels (pixbuf));

  g_object_set_qdata_full (G_OBJECT (bmp),
                           pixbuf_key,
                           pixbuf,
                           g_object_unref);

  return bmp;
}

static inline CoglTexture *
clutter_test_texture_2d_sliced_new_from_file (CoglContext  *ctx,
                                              const char   *filename,
                                              GError      **error)
{
  CoglBitmap *bmp;
  CoglTexture *tex_2ds = NULL;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  bmp = clutter_test_create_bitmap_from_file (ctx, filename, error);
  if (bmp == NULL)
    return NULL;

  tex_2ds = cogl_texture_2d_sliced_new_from_bitmap (bmp, COGL_TEXTURE_MAX_WASTE);

  g_object_unref (bmp);

  return tex_2ds;
}

static inline CoglTexture *
clutter_test_texture_2d_new_from_file (CoglContext  *ctx,
                                       const char   *filename,
                                       GError      **error)
{
  CoglBitmap *bmp;
  CoglTexture *tex_2d = NULL;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  bmp = clutter_test_create_bitmap_from_file (ctx, filename, error);
  if (bmp == NULL)
    return NULL;

  tex_2d = cogl_texture_2d_new_from_bitmap (bmp);

  g_object_unref (bmp);

  return tex_2d;
}
