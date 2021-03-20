#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

#include <utils/utils_visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { unsigned unused; } * rkv;

typedef bool (* rkv_codec_operation)( const void * dest, const void * src, size_t n, const void * user_context );

typedef struct {
   unsigned            typeid;
   rkv_codec_operation encoder;
   rkv_codec_operation decoder;
} rkv_codec;

extern const rkv_codec rkv_codec_Zero;

typedef struct {
   long     hostid;
   pid_t    pid;
   unsigned instanceid;
} rkv_id;

typedef struct {
   rkv_id   id;
   unsigned typeid;
   void *   data;
} rkv_data;

extern const rkv_data rkv_data_Zero;

DLL_PUBLIC bool rkv_new      ( rkv * This, const char * group, unsigned short port );
DLL_PUBLIC bool rkv_add_codec( rkv * This, rkv_codec * codec );
DLL_PUBLIC bool rkv_put      ( rkv   This, const rkv_data * data );
DLL_PUBLIC bool rkv_get      ( rkv   This, rkv_data * data );
DLL_PUBLIC bool rkv_publish  ( rkv   This );
DLL_PUBLIC bool rkv_refresh  ( rkv   This );
DLL_PUBLIC bool rkv_delete   ( rkv * This );

#ifdef __cplusplus
}
#endif
