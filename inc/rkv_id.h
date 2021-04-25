#pragma once

#include <net/net_buff.h>

typedef struct { unsigned unused; } * rkv_id;

// 0000000001/0000021314@007f0101
#define ID_AS_STRING_LENGTH_MAX (10+1+10+1+8)

DLL_PUBLIC bool rkv_id_new      ( rkv_id * id );
DLL_PUBLIC bool rkv_id_encode   ( const rkv_id id, net_buff buffer );
DLL_PUBLIC bool rkv_id_decode   ( rkv_id * id, net_buff buffer );
DLL_PUBLIC bool rkv_id_to_string( const rkv_id id, char * dest, size_t dest_size );
DLL_PUBLIC bool rkv_id_delete   ( rkv_id * id );

DLL_PUBLIC int  rkv_id_compare( const void * l, const void * r );
