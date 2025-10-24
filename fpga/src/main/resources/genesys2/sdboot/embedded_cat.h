#ifndef EMBEDDED_CAT_H
#define EMBEDDED_CAT_H

/* Provide a minimal size_t typedef for freestanding/bare-metal builds.
 * If your toolchain provides <stddef.h> and you want to use it, build
 * with -DJPG_PROVIDE_STD_TYPES=0 and include <stddef.h> before this header. */
#ifndef JPG_PROVIDE_STD_TYPES
#define JPG_PROVIDE_STD_TYPES 1
#endif
#if JPG_PROVIDE_STD_TYPES
#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned long size_t;
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern const unsigned char embedded_cat[];
extern const size_t embedded_cat_size;

#ifdef __cplusplus
}
#endif

#endif /* EMBEDDED_CAT_H */
