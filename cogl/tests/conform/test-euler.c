#include <cogl/cogl.h>
#include <math.h>
#include <string.h>

#include "test-declarations.h"
#include "test-utils.h"

/* Macros are used here instead of functions so that the
 * g_assert_cmpfloat will give a more interesting message when it
 * fails */

#define COMPARE_FLOATS(a, b)                    \
  do {                                          \
    if (fabsf ((a) - (b)) >= 0.0001f)           \
      g_assert_cmpfloat ((a), ==, (b));         \
  } while (0)

#define COMPARE_ELEMENT(a, b, idx)        \
  do {                                          \
    COMPARE_FLOATS (a[idx], b[idx]);            \
  } while (0)

#define COMPARE_MATRICES(a, b) \
  do {                                          \
    const float *af = cogl_matrix_get_array (a);\
    const float *bf = cogl_matrix_get_array (b);\
    int i;                                      \
                                                \
    for (i = 0; i < 16; i++)                    \
      COMPARE_ELEMENT (af, bf, i);              \
  } while (0)

void
test_euler (void)
{
  graphene_euler_t euler;
  CoglMatrix matrix_a, matrix_b;

  /* Try doing the rotation with three separate rotations */
  cogl_matrix_init_identity (&matrix_a);
  cogl_matrix_rotate (&matrix_a, -30.0f, 0.0f, 1.0f, 0.0f);
  cogl_matrix_rotate (&matrix_a, 40.0f, 1.0f, 0.0f, 0.0f);
  cogl_matrix_rotate (&matrix_a, 50.0f, 0.0f, 0.0f, 1.0f);

  /* And try the same rotation with a euler */
  graphene_euler_init_with_order (&euler, 40, -30, 50, GRAPHENE_EULER_ORDER_RYXZ);
  cogl_matrix_init_from_euler (&matrix_b, &euler);

  /* Verify that the matrices are approximately the same */
  COMPARE_MATRICES (&matrix_a, &matrix_b);

  /* Try applying the rotation from a euler to a framebuffer */
  cogl_framebuffer_identity_matrix (test_fb);
  cogl_framebuffer_rotate_euler (test_fb, &euler);
  memset (&matrix_b, 0, sizeof (matrix_b));
  cogl_framebuffer_get_modelview_matrix (test_fb, &matrix_b);
  COMPARE_MATRICES (&matrix_a, &matrix_b);

  /* FIXME: This needs a lot more tests! */

  if (cogl_test_verbose ())
    g_print ("OK\n");
}
