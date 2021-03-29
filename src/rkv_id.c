#include <rkv.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
   long     host;
   pid_t    process;
   unsigned instance;
} rkv_id_private;

static unsigned instance_allocator = 1;

bool rkv_id_new( rkv_id * id ) {
   if( id == NULL ) {
      fprintf( stderr, "%s: NULL argument\n", __func__ );
      return false;
   }
   rkv_id_private * This = malloc( sizeof( rkv_id_private ));
   if( This == NULL ) {
      perror( "malloc" );
      return false;
   }
   This->host     = gethostid();
   This->process  = getpid();
   This->instance = instance_allocator++;
   *id = (rkv_id)This;
   return true;
}

bool rkv_id_encode( const rkv_id id, net_buff buffer ) {
   if(( id == NULL )||( buffer == NULL )) {
      fprintf( stderr, "%s: NULL argument\n", __func__ );
      return false;
   }
   rkv_id_private * This = (rkv_id_private *)id;
   return net_buff_encode_int32 ( buffer, (int32_t)This->host )
      &&  net_buff_encode_int32 ( buffer, This->process  )
      &&  net_buff_encode_uint32( buffer, This->instance );
}

bool rkv_id_decode( rkv_id * id, net_buff buffer ) {
   if(( id == NULL )||( buffer == NULL )) {
      fprintf( stderr, "%s: NULL argument\n", __func__ );
      return false;
   }
   int32_t  host     = 0;
   int32_t  process  = 0;
   uint32_t instance = 0;
   if(   net_buff_decode_int32 ( buffer, &host     )
      && net_buff_decode_int32 ( buffer, &process  )
      && net_buff_decode_uint32( buffer, &instance ))
   {
      rkv_id_private * This = malloc( sizeof( rkv_id_private ));
      memset( This, 0, sizeof( rkv_id_private ));
      *id = (rkv_id)This;
      This->host     = host;
      This->process  = process;
      This->instance = instance;
      return true;
   }
   return false;
}

bool rkv_id_to_string( const rkv_id id, char * dest, size_t dest_size ) {
   if(( id == NULL )||( dest == NULL )) {
      fprintf( stderr, "%s: NULL argument\n", __func__ );
      return false;
   }
   rkv_id_private * This = (rkv_id_private *)id;
   return snprintf( dest, dest_size, "%010u/%010d@%08lx", This->instance, This->process, This->host ) > 0;
}

bool rkv_id_delete( rkv_id * id ) {
   if( id == NULL ) {
      fprintf( stderr, "%s: NULL argument\n", __func__ );
      return false;
   }
   free( *id );
   *id = NULL;
   return true;
}

int rkv_id_compare( const void * l, const void * r ) {
   const rkv_id_private * const * pl    = (const rkv_id_private * const *)l;
   const rkv_id_private * const * pr    = (const rkv_id_private * const *)r;
   const rkv_id_private *         left  = *pl;
   const rkv_id_private *         right = *pr;
   int diff = (int)( left->host - right->host );
   if( diff == 0 ) {
      diff = (int)( left->process - right->process );
      if( diff == 0 ) {
         diff = (int)( left->instance - right->instance );
      }
   }
   return diff;
}
