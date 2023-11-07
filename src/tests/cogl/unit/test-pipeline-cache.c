#include "config.h"

#include "cogl/cogl.h"
#include "cogl/cogl-pipeline-cache-private.h"
#include "cogl/cogl-pipeline-hash-table.h"
#include "tests/cogl-test-utils.h"

static void
create_pipelines (CoglPipeline **pipelines,
                  int n_pipelines)
{
  int i;

  for (i = 0; i < n_pipelines; i++)
    {
      char fraction[G_ASCII_DTOSTR_BUF_SIZE];
      g_autofree char *source = NULL;
      CoglSnippet *snippet;

      g_ascii_dtostr (fraction, sizeof (fraction), i / 255.0);
      source = g_strdup_printf ("  cogl_color_out = "
                                "vec4 (%s, 0.0, 0.0, 1.0);\n",
                                fraction);
      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                  NULL, /* declarations */
                                  source);

      pipelines[i] = cogl_pipeline_new (test_ctx);
      cogl_pipeline_add_snippet (pipelines[i], snippet);
      g_object_unref (snippet);
    }

  /* Test that drawing with them works. This should create the entries
   * in the cache */
  for (i = 0; i < n_pipelines; i++)
    {
      cogl_framebuffer_draw_rectangle (test_fb,
                                       pipelines[i],
                                       i, 0,
                                       i + 1, 1);
      test_utils_check_pixel_rgb (test_fb, i, 0, i, 0, 0);
    }

}

static void
check_pipeline_pruning (void)
{
  CoglPipeline *pipelines[18];
  int fb_width, fb_height;
  CoglPipelineHashTable *fragment_hash =
    cogl_pipeline_cache_get_fragment_hash (test_ctx->pipeline_cache);
  CoglPipelineHashTable *combined_hash =
    cogl_pipeline_cache_get_combined_hash (test_ctx->pipeline_cache);
  int i;

  fb_width = cogl_framebuffer_get_width (test_fb);
  fb_height = cogl_framebuffer_get_height (test_fb);

  cogl_framebuffer_orthographic (test_fb,
                                 0, 0,
                                 fb_width,
                                 fb_height,
                                 -1,
                                 100);

  /* Create 18 unique pipelines. This should end up being more than
   * the initial expected minimum size so it will trigger the garbage
   * collection. However all of the pipelines will be in use so they
   * won't be collected */
  create_pipelines (pipelines, 18);

  /* These pipelines should all have unique entries in the cache. We
   * should have run the garbage collection once and at that point the
   * expected minimum size would have been 17 */
  g_assert_cmpint (g_hash_table_size (fragment_hash->table), ==, 18);
  g_assert_cmpint (g_hash_table_size (combined_hash->table), ==, 18);
  g_assert_cmpint (fragment_hash->expected_min_size, ==, 17);
  g_assert_cmpint (combined_hash->expected_min_size, ==, 17);

  /* Destroy the original pipelines and create some new ones. This
   * should run the garbage collector again but this time the
   * pipelines won't be in use so it should free some of them */
  for (i = 0; i < 18; i++)
    g_object_unref (pipelines[i]);

  create_pipelines (pipelines, 18);

  /* The garbage collection should have freed half of the original 18
   * pipelines which means there should now be 18*1.5 = 27 */
  g_assert_cmpint (g_hash_table_size (fragment_hash->table), ==, 27);
  g_assert_cmpint (g_hash_table_size (combined_hash->table), ==, 27);
  /* The 35th pipeline would have caused the garbage collection. At
   * that point there would be 35-18=17 used unique pipelines. */
  g_assert_cmpint (fragment_hash->expected_min_size, ==, 17);
  g_assert_cmpint (combined_hash->expected_min_size, ==, 17);

  for (i = 0; i < 18; i++)
    g_object_unref (pipelines[i]);
}

COGL_TEST_SUITE (
  g_test_add_func ("/pipeline-cache/pruning", check_pipeline_pruning);
)
