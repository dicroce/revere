/* Revere addition: rnnoise 0.2's vec.h (generic path) and vec_neon.h include
   "os_support.h", but the 0.2 release tarball doesn't ship it, which breaks
   non-x86 builds (e.g. aarch64 / Apple Silicon / Raspberry Pi). Only
   OPUS_CLEAR is needed from it; this matches the Opus definition. */

#ifndef OS_SUPPORT_H
#define OS_SUPPORT_H

#include <string.h>

#ifndef OPUS_CLEAR
#define OPUS_CLEAR(dst, n) (memset((dst), 0, (n)*sizeof(*(dst))))
#endif

#endif
