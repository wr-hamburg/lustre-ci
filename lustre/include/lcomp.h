#ifndef _LCOMPRESSION_H
#define _LCOMPRESSION_H
#endif

/* LZ4 */
#if defined(HAVE_KERNEL_LZ4_COMPRESS) || defined(HAVE_KERNEL_LZ4_COMPRESS_FAST)
#include <linux/lz4.h>
#else
#include "../obdclass/compression/lz4/lz4.h" /* TODO more generic path */
#endif
