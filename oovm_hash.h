#ifndef __OVM_HASH_H
#define __OVM_HASH_H

#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */
  
static inline unsigned
mem_hash(unsigned size, const void *data)
{
  return (crc32(-1, (unsigned char *) data, size));
}

#ifdef __cplusplus
}
#endif /* defined(__cplusplus) */

#endif /* __OVM_HASH_H */
