#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

#include <net/net_buff.h>
#include <utils/utils_map.h>

#include <rkv_id.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (* rkv_encode_operation )( net_buff buffer, const void * src, utils_map codecs );
typedef bool (* rkv_decode_operation )( void * dest, net_buff buffer, utils_map codecs );
typedef void (* rkv_release_operation)( void * data, utils_map codecs );

typedef struct {
   unsigned              type;
   rkv_encode_operation  encoder;
   rkv_decode_operation  factory;
   rkv_release_operation releaser;
} rkv_codec;

extern const rkv_codec rkv_codec_Zero;

typedef struct { unsigned unused; } * rkv;

DLL_PUBLIC bool rkv_new    ( rkv * cache, const char * group, unsigned short port, const rkv_codec * const codecs[], size_t codec_count );
DLL_PUBLIC bool rkv_put    ( rkv   cache, const char * transaction, const rkv_id id, unsigned type, const void * data );
DLL_PUBLIC bool rkv_publish( rkv   cache, const char * transaction );
DLL_PUBLIC bool rkv_refresh( rkv   cache );
DLL_PUBLIC bool rkv_get    ( rkv   cache, const rkv_id id, const void ** data );
DLL_PUBLIC bool rkv_delete ( rkv * cache );

#ifdef __cplusplus
}
#endif
