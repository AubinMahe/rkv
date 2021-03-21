#pragma once

typedef struct {
   long     host;
   pid_t    process;
   unsigned instance;
} rkv_id;

DLL_PUBLIC extern const rkv_id rkv_id_Zero;

DLL_PUBLIC bool rkv_id_init   (       rkv_id * id, unsigned instance );
DLL_PUBLIC bool rkv_id_encode ( const rkv_id * id, net_buff buffer );
DLL_PUBLIC bool rkv_id_decode (       rkv_id * id, net_buff buffer );
DLL_PUBLIC int  rkv_id_compare( const void * l, const void * r );
