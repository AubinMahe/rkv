#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

#include <net/net_buff.h>
#include <utils/utils_map.h>

#include "rkv_id.h"

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
typedef const void * rkv_value;

typedef void (* rkv_change_callback )( rkv cache, void * user_context );
typedef bool (* rkv_iterator )( size_t index, const rkv_id id, unsigned type, rkv_value data, void * user_context );

DLL_PUBLIC bool rkv_new         ( rkv * cache, const char * group, unsigned short port, const rkv_codec * const codecs[], size_t count );
DLL_PUBLIC bool rkv_add_listener( rkv   cache, rkv_change_callback callback, void * user_context );
DLL_PUBLIC bool rkv_put         ( rkv   cache, const char * transaction, const rkv_id id, unsigned type, rkv_value data );
DLL_PUBLIC bool rkv_publish     ( rkv   cache, const char * transaction );
DLL_PUBLIC bool rkv_refresh     ( rkv   cache );
DLL_PUBLIC bool rkv_get         ( rkv   cache, const rkv_id id, rkv_value * data );
DLL_PUBLIC bool rkv_get_ids     ( rkv   cache, rkv_id target[], size_t * target_size );
DLL_PUBLIC bool rkv_foreach     ( rkv   cache, rkv_iterator iterator, void * user_context );
DLL_PUBLIC bool rkv_delete      ( rkv * cache );

#ifdef __cplusplus
}
#endif
