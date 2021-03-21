#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

#include <net/net_buff.h>

#include <rkv_id.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (* rkv_encode_operation)( net_buff buffer, const void * src, void * user_context );
typedef bool (* rkv_decode_operation)( void * dest, net_buff buffer, void * user_context );

typedef struct {
   unsigned             type;
   rkv_encode_operation encoder;
   rkv_decode_operation decoder;
} rkv_codec;

extern const rkv_codec rkv_codec_Zero;

typedef struct { unsigned unused; } * rkv;

DLL_PUBLIC bool rkv_new      ( rkv * cache, const char * group, unsigned short port );
DLL_PUBLIC bool rkv_add_codec( rkv   cache, const rkv_codec * codec );
DLL_PUBLIC bool rkv_put      ( rkv   cache, const rkv_id * id, unsigned type, void * data );
DLL_PUBLIC bool rkv_publish  ( rkv   cache );
DLL_PUBLIC bool rkv_refresh  ( rkv   cache );
DLL_PUBLIC bool rkv_get      ( rkv   cache, const rkv_id * id, void ** data );
DLL_PUBLIC bool rkv_delete   ( rkv * cache );

#ifdef __cplusplus
}
#endif
