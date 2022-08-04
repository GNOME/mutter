#undef COGL_COMPILATION
#include <cogl/cogl.h>

/* If you just include cogl/cogl.h, you shouldn't end up including any
   GL headers */
#ifdef GL_TRUE
#error "Including cogl.h shouldn't be including any GL headers"
#endif

int
main (int    argc,
      char **argv)
{
  return EXIT_SUCCESS;
}
