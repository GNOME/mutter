/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2022 Red Hat Inc
 * Copyright 2008 Dennis Smit
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
 */

/* CPU capability discovery code comes from lp_cpu_detect.c in mesa. */

#include "config.h"

#include "cogl/cogl-cpu-caps.h"

#include <stdint.h>

CoglCpuCaps cogl_cpu_caps;

#ifdef __x86_64
static inline uint64_t
xgetbv (void)
{
#ifdef __GCC_ASM_FLAG_OUTPUTS__
  uint32_t eax, edx;

  __asm __volatile (
    ".byte 0x0f, 0x01, 0xd0" /* xgetbv isn't supported on gcc < 4.4 */
    : "=a"(eax),
      "=d"(edx)
    : "c"(0)
  );

  return ((uint64_t) edx << 32) | eax;
#else
  return 0;
#endif
}

static inline void
cpuid (uint32_t  ax,
       uint32_t *p)
{
#ifdef __GCC_ASM_FLAG_OUTPUTS__
   __asm __volatile (
     "cpuid\n\t"
     : "=a" (p[0]),
       "=b" (p[1]),
       "=c" (p[2]),
       "=d" (p[3])
     : "0" (ax)
   );
#else
   p[0] = 0;
   p[1] = 0;
   p[2] = 0;
   p[3] = 0;
#endif
}
#endif

void
cogl_init_cpu_caps (void)
{
#ifdef __x86_64
  uint32_t regs[4];

  cpuid (0x00000000, regs);

  if (regs[0] >= 0x00000001)
    {
      uint32_t regs2[4];
      gboolean has_avx;

      cpuid (0x00000001, regs2);

      has_avx = (((regs2[2] >> 28) & 1) && /* AVX */
                 ((regs2[2] >> 27) & 1) && /* OSXSAVE */
                 ((xgetbv () & 6) == 6));   /* XMM & YMM */
      if (((regs2[2] >> 29) & 1) && has_avx)
        cogl_cpu_caps |= COGL_CPU_CAP_F16C;
    }
#endif
}
