#include <rkv.h>
#include <stdint.h>
#include <stdio.h>

const rkv_id rkv_id_Zero = { 0L, 0, 0U };

bool rkv_id_init( rkv_id * This, unsigned instance ) {
   if( This == NULL ) {
      fprintf( stderr, "%s: NULL argument\n", __func__ );
      return false;
   }
   This->host     = gethostid();
   This->process  = getpid();
   This->instance = instance;
   return true;
}

bool rkv_id_encode( const rkv_id * This, net_buff buffer ) {
   if(( This == NULL )||( buffer == NULL )) {
      fprintf( stderr, "%s: NULL argument\n", __func__ );
      return false;
   }
   return net_buff_encode_int32 ( buffer, (int32_t)This->host )
      &&  net_buff_encode_int32 ( buffer, This->process  )
      &&  net_buff_encode_uint32( buffer, This->instance );
}

bool rkv_id_decode( rkv_id * This, net_buff buffer ) {
   if(( This == NULL )||( buffer == NULL )) {
      fprintf( stderr, "%s: NULL argument\n", __func__ );
      return false;
   }
   return net_buff_decode_int32 ( buffer, (int32_t*)&This->host )
      &&  net_buff_decode_int32 ( buffer, &This->process  )
      &&  net_buff_decode_uint32( buffer, &This->instance );
}

int rkv_id_compare( const void * l, const void * r ) {
   const rkv_id * const * pl    = (const rkv_id * const *)l;
   const rkv_id * const * pr    = (const rkv_id * const *)r;
   const rkv_id *         left  = *pl;
   const rkv_id *         right = *pr;
   int diff = (int)( left->host - right->host );
   if( diff == 0 ) {
      diff = (int)( left->process - right->process );
      if( diff == 0 ) {
         diff = (int)( left->instance - right->instance );
      }
   }
   return diff;
}
