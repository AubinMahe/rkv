#include <rkv.h>

#include <net/net_buff.h>
#include <utils/utils_map.h>

#include <ifaddrs.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// Astuce pour libérer un pointeur const void * en espérant qu'il ne soit pas "vraiment" const,
// sinon c'est SIGSEGV !
// Ne fonctionne que si les pointeurs sont stockables sur 64 bits
#define CONST_CAST(p,T)       ((T *)(uint64_t)(p))
#define MCAST_MIN             9
#define MCAST_MAX             11
#define PAYLOAD_MAX           (64*1024)
#define NET_ID_MAX            (10+1+15)
#define RKV_DBG               false
#define RKV_DBG_DUMP_RECV     false
#define RKV_DBG_MEMORY        false

const rkv_codec rkv_codec_Zero = { 0U, NULL, NULL, NULL };

typedef struct {
   rkv_id       id;
   unsigned     type;
   const void * payload;
} rkv_data_holder;

typedef struct rkv_listener_s {
   rkv_change_callback callback;
   void *              user_context;
   struct rkv_listener_s * next;
} * rkv_listener;

/**
 * Cette classe contient plusieurs caches :
 * - Le cache courant de l'application, en lecture seule.
 * - Un cache par transaction dédié à l'écriture de nouvelles valeurs.
 * - Un cache alimenté par la réception de valeurs provenant du réseau.
 *
 * Les caches de transaction sont publiés sur le réseau sur demande explicite
 * de l'application, par un appel à publish().
 *
 * Le cache de réception est mergé dans le cache courant, en lecture seule,
 * sur demande explicite de l'application, par un appel à refresh().
 */
typedef struct {
   int                sckt;
   struct sockaddr_in recv_addr;
   struct sockaddr_in send_addr;
   struct ip_mreq     imr;
   bool               is_alive;
   char               localID[NET_ID_MAX];
   net_buff           recv_buff;
   net_buff           send_buff;
   pthread_t          thread;
   utils_map          codecs;
   utils_map          read_only_data;
   utils_map          transactions;
   pthread_mutex_t    received_data_lock;
   utils_map          received_data;
   rkv_listener       listeners;
} rkv_private;

static bool get_multicast_interface_address( char * address ) {
   struct ifaddrs * ifaddr = NULL;
   if( getifaddrs( &ifaddr )) {
      perror( "getifaddrs" );
      return false;
   }
   unsigned mask = IFF_MULTICAST | IFF_UP;
   for( struct ifaddrs * ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next ) {
      if((( ifa->ifa_flags & mask ) == mask )&&( ifa->ifa_addr->sa_family == AF_INET )) {
         struct sockaddr_in * addr = (struct sockaddr_in *)ifa->ifa_addr;
         inet_ntop( AF_INET, &addr->sin_addr, address, INET_ADDRSTRLEN );
         break;
      }
   }
   freeifaddrs( ifaddr );
   return true;
}

typedef struct {
   char * dest;
   size_t dest_size;
} string;

static bool dump_one_id( size_t index, const void * key, const void * value, void * user_context ) {
   string * str = user_context;
   char ids[ID_AS_STRING_LENGTH_MAX+1];
   void * id = CONST_CAST( key, void );
   rkv_id_to_string( id, ids, sizeof( ids ));
   size_t len = strlen( str->dest );
   if( len &&( len+2 < str->dest_size )) {
      strcat( str->dest, ", " );
      len += 2;
   }
   if( len + ID_AS_STRING_LENGTH_MAX < str->dest_size ) {
      strcat( str->dest, ids );
   }
   return true;
   (void)index;
   (void)value;
}

static void dump_all_ids( utils_map map, const char * title ) {
   size_t count = 0;
   utils_map_get_size( map, &count );
   char buffer[count*(ID_AS_STRING_LENGTH_MAX+2)];
   string str = { .dest = buffer, .dest_size = sizeof( buffer )};
   memset( str.dest, 0, str.dest_size );
   utils_map_foreach( map, dump_one_id, &str );
   fprintf( stderr, "%s: %ld: %s\n", title, count, str.dest );
}

static bool is_alive( rkv_private * This ) {
   bool is_alive = false;
   pthread_mutex_lock( &This->received_data_lock );
   is_alive = This->is_alive;
   pthread_mutex_unlock( &This->received_data_lock );
   return is_alive;
}

static void * multicast_receive_thread( void * arg ) {
   rkv_private * This = (rkv_private *)arg;
   This->is_alive = true;
   while( is_alive( This )) {
      net_buff_clear( This->recv_buff );
      if( net_buff_receive( This->recv_buff, This->sckt, &This->recv_addr )) {
         size_t position = 0;
         if(   net_buff_get_position( This->recv_buff, &position ) &&( position > 0 )
            && net_buff_flip( This->recv_buff ))
         {
            if( RKV_DBG ) {
               size_t limit = 0;
               if( net_buff_get_limit( This->recv_buff, &limit )) {
                  struct timeval tv;
                  gettimeofday( &tv, NULL );
                  fprintf( stderr, "%6ld.%06ld:DEBUG:%s:packet received, %ld bytes\n", tv.tv_sec, tv.tv_usec, __func__, limit );
                  if( RKV_DBG_DUMP_RECV ) {
                     char dump[20*80];
                     if( net_buff_dump( This->recv_buff, dump, sizeof( dump ))) {
                        fprintf( stderr, "%s|%s", __func__, dump );
                     }
                  }
               }
            }
            utils_map received_data = NULL;
            pthread_mutex_lock( &This->received_data_lock );
            received_data = This->received_data;
            pthread_mutex_unlock( &This->received_data_lock );
            if( received_data == NULL ) {
               utils_map_new( &received_data, rkv_id_compare, true, true );
            }
            rkv_id id = NULL;
            size_t limit = 0;
            position = 0;
            while( net_buff_get_position( This->recv_buff, &position )
               &&  net_buff_get_limit   ( This->recv_buff, &limit    )
               &&( position < limit )
               &&  rkv_id_decode( &id, This->recv_buff ))
            {
               unsigned type;
               if( ! net_buff_decode_uint32( This->recv_buff, &type )) {
                  char ids[ID_AS_STRING_LENGTH_MAX+1];
                  rkv_id_to_string( id, ids, sizeof( ids ));
                  fprintf( stderr, "%s: unable to decode type of %s of type %d, packet skipped", __func__, ids, type );
                  break;
               }
               rkv_codec * codec = NULL;
               if(( ! utils_map_get( This->codecs, &type, (void **)&codec ))||( codec == NULL )) {
                  char ids[ID_AS_STRING_LENGTH_MAX+1];
                  rkv_id_to_string( id, ids, sizeof( ids ));
                  fprintf( stderr, "%s: no codec found for %s, packet skipped\n", __func__, ids );
                  break;
               }
               rkv_data_holder * entry = malloc( sizeof( rkv_data_holder));
               if( entry == NULL ) {
                  perror( "malloc" );
                  pthread_mutex_lock( &This->received_data_lock );
                  This->is_alive = false;
                  pthread_mutex_unlock( &This->received_data_lock );
                  break;
               }
               entry->id   = id;
               entry->type = type;
               entry->payload = NULL;
               if( ! codec->factory( &entry->payload, This->recv_buff, This->codecs )) {
                  char ids[ID_AS_STRING_LENGTH_MAX+1];
                  rkv_id_to_string( id, ids, sizeof( ids ));
                  fprintf( stderr, "%s: unable to decode data %s of type %d, packet skipped\n", __func__, ids, type );
                  break;
               }
               if( RKV_DBG_MEMORY ) {
                  fprintf( stderr, "%s|utils_map_put( key = %p, value = %p )\n", __func__, (void *)id, (void *)entry );
               }
               if( ! utils_map_put( received_data, id, entry )) {
                  char ids[ID_AS_STRING_LENGTH_MAX+1];
                  rkv_id_to_string( id, ids, sizeof( ids ));
                  fprintf( stderr, "%s: unable to store data %s of type %d\n", __func__, ids, type );
               }
            }
            if( RKV_DBG ) {
               struct timeval tv;
               gettimeofday( &tv, NULL );
               fprintf( stderr, "%6ld.%06ld:DEBUG:%s:", tv.tv_sec, tv.tv_usec, __func__ );
               dump_all_ids( received_data, __func__ );
            }
            pthread_mutex_lock( &This->received_data_lock );
            This->received_data = received_data;
            pthread_mutex_unlock( &This->received_data_lock );
            for( rkv_listener listener = This->listeners; listener; listener = listener->next ) {
               listener->callback((rkv)This, listener->user_context );
            }
         }
      }
   }
   return NULL;
}

static int codec_id_compare( const void * l, const void * r ) {
   const unsigned * const * pl    = (const unsigned * const *)l;
   const unsigned * const * pr    = (const unsigned * const *)r;
   const unsigned *         left  = *pl;
   const unsigned *         right = *pr;
   return (int)( *left - *right );
}

static int string_compare( const void * l, const void * r ) {
   const char * const * pl    = (const char * const *)l;
   const char * const * pr    = (const char * const *)r;
   const char *         left  = *pl;
   const char *         right = *pr;
   return strcmp( left, right );
}

bool rkv_new( rkv * cache, const char * group, unsigned short port, const rkv_codec * const codecs[], size_t codec_count ) {
#ifdef _WIN32
   WSADATA wsaData;
   if( WSAStartup( 0x0101, &wsaData )) {
      perror( "WSAStartup" );
      return false;
   }
#endif
   if(( cache == NULL )||( group == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   *cache = NULL;
   rkv_private * This = malloc( sizeof( rkv_private ));
   if( This == NULL ) {
      return false;
   }
   memset( This, 0, sizeof( rkv_private ));
   This->sckt     = -1;
   This->is_alive = false;
   if( strlen( group ) < MCAST_MIN ) {
      fprintf( stderr, "%s: multicast IP v4 address too short: %s, expected 239.0.0.[0..255]\n", __func__, group );
      free( This );
      return false;
   }
   if( strlen( group ) > MCAST_MAX ) {
      fprintf( stderr, "%s: multicast IP v4 address too long: %s, expected 239.0.0.[0..255]\n", __func__, group );
      free( This );
      return false;
   }
   memset( &This->recv_addr, 0, sizeof( This->recv_addr ));
   This->recv_addr.sin_family      = AF_INET;
   This->recv_addr.sin_port        = htons( port );
   This->recv_addr.sin_addr.s_addr = htonl( INADDR_ANY );
   memset( &This->send_addr, 0, sizeof( This->send_addr ));
   This->send_addr.sin_family      = AF_INET;
   This->send_addr.sin_port        = htons( port );
   This->send_addr.sin_addr.s_addr = inet_addr( group );
   memset( &This->imr, 0, sizeof( This->imr ));
   This->imr.imr_multiaddr.s_addr = inet_addr( group );
   This->imr.imr_interface.s_addr = htonl( INADDR_ANY );
   This->sckt = socket( PF_INET, SOCK_DGRAM, 0 );
   if( This->sckt < 0 ) {
      perror( "socket( PF_INET, SOCK_DGRAM )" );
      free( This );
      return false;
   }
   unsigned yes = 1;
   if( setsockopt( This->sckt, SOL_SOCKET, SO_REUSEADDR, (char*) &yes, sizeof( yes )) < 0 ) {
      perror( "setsockopt( SOL_SOCKET, SO_REUSEADDR )" );
      close( This->sckt );
      free( This );
      return false;
   }
   if( bind( This->sckt, (struct sockaddr*) &This->recv_addr, sizeof( This->recv_addr )) < 0 ) {
      perror( "bind" );
      close( This->sckt );
      free( This );
      return false;
   }
   if( setsockopt( This->sckt, IPPROTO_IP, IP_ADD_MEMBERSHIP, &This->imr, sizeof( This->imr )) < 0 ) {
      perror( "setsockopt( IP_ADD_MEMBERSHIP )" );
      close( This->sckt );
      free( This );
      return false;
   }
   const pid_t    pid    = getpid();
   const long int hostid = gethostid();
   char           ipv4[INET_ADDRSTRLEN];
   if( ! get_multicast_interface_address( ipv4 )) {

   }
   snprintf( This->localID, sizeof( This->localID ), "%d/%ld@%s:%d", pid, hostid, ipv4, This->recv_addr.sin_port );
   if( ! net_buff_new( &This->recv_buff, PAYLOAD_MAX )) {
      close( This->sckt );
      free( This );
      return false;
   }
   if( ! net_buff_new( &This->send_buff, PAYLOAD_MAX )) {
      close( This->sckt );
      net_buff_delete( &This->recv_buff );
      free( This );
      return false;
   }
   if( ! utils_map_new( &This->codecs, codec_id_compare, false, true )) {
      close( This->sckt );
      net_buff_delete( &This->recv_buff );
      net_buff_delete( &This->send_buff );
      free( This );
      return false;
   }
   for( size_t i = 0; i < codec_count; ++i ) {
      const rkv_codec * const codec = codecs[i];
      rkv_codec * value = malloc( sizeof( rkv_codec ));
      if( value == NULL ) {
         perror( "malloc rkv_codec" );
         return false;
      }
      *value = *codec;
      if( ! utils_map_put( This->codecs, &value->type, value )) {
         free( value );
         return false;
      }
   }
   if( ! utils_map_new( &This->read_only_data, rkv_id_compare, true, true )) {
      close( This->sckt );
      net_buff_delete( &This->recv_buff );
      net_buff_delete( &This->send_buff );
      utils_map_delete( &This->codecs );
      free( This );
      return false;
   }
//   utils_map_set_trace( This->read_only_data, UTILS_MAP_TRACE_FREE );
   if( ! utils_map_new( &This->transactions, string_compare, false, false )) {
      close( This->sckt );
      net_buff_delete( &This->recv_buff );
      net_buff_delete( &This->send_buff );
      utils_map_delete( &This->codecs );
      utils_map_delete( &This->read_only_data );
      free( This );
      return false;
   }
   pthread_mutex_init( &This->received_data_lock, NULL );
   if( pthread_create( &This->thread, NULL, multicast_receive_thread, This )) {
      perror( "pthread_create" );
      close( This->sckt );
      net_buff_delete ( &This->recv_buff );
      net_buff_delete ( &This->send_buff );
      utils_map_delete( &This->codecs );
      utils_map_delete( &This->read_only_data );
      utils_map_delete( &This->transactions );
      free( This );
      return false;
   }
   *cache = (rkv)This;
   return true;
}

bool rkv_add_listener( rkv cache, rkv_change_callback callback, void * user_context ) {
   if(( cache == NULL )||( callback == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_listener listener = malloc( sizeof( struct rkv_listener_s ));
   if( listener == NULL ) {
      perror( "malloc" );
      return false;
   }
   listener->callback     = callback;
   listener->user_context = user_context;
   rkv_private * This = (rkv_private *)cache;
   listener->next = This->listeners;
   This->listeners = listener;
   return true;
}

bool rkv_put( rkv cache, const char * name, const rkv_id id, unsigned type, const void * data ) {
   if(( cache == NULL )||( name == NULL )||( id == NULL )||( data == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This  = (rkv_private *)cache;
   utils_map transaction = NULL;
   if( ! utils_map_get( This->transactions, name, (void **)&transaction )) {
      if( ! utils_map_new( &transaction, rkv_id_compare, false, true )) {
         return false;
      }
      if( ! utils_map_put( This->transactions, name, transaction )) {
         return false;
      }
   }
   rkv_data_holder * entry = malloc( sizeof( rkv_data_holder ));
   entry->id   = id;
   entry->type = type;
   entry->payload = data;
   if( ! utils_map_put( transaction, id, entry )) {
      free( entry );
      return false;
   }
   return true;
}

static bool rkv_data_encode( size_t index, const void * key, const void * value, void * user_context ) {
   rkv_private *    This  = (rkv_private *)user_context;
   const rkv_data_holder * data  = value;
   rkv_codec *      codec = NULL;
   if(   rkv_id_encode( data->id, This->send_buff )
      && net_buff_encode_uint32( This->send_buff, data->type ))
   {
      if( ! utils_map_get( This->codecs, &data->type, (void **)&codec )) {
         char ids[ID_AS_STRING_LENGTH_MAX+1];
         rkv_id_to_string( data->id, ids, sizeof( ids ));
         fprintf( stderr, "%s: unable to encode data %s of type %d (no codec found)\n", __func__, ids, data->type );
      }
      else if( ! codec->encoder( This->send_buff, data->payload, This->codecs )) {
         char ids[ID_AS_STRING_LENGTH_MAX+1];
         rkv_id_to_string( data->id, ids, sizeof( ids ));
         fprintf( stderr, "%s: unable to encode data %s of type %d (encoder failed)\n", __func__, ids, data->type );
      }
   }
   else {
      char ids[ID_AS_STRING_LENGTH_MAX+1];
      rkv_id_to_string( data->id, ids, sizeof( ids ));
      fprintf( stderr, "%s: unable to encode header of %s of type %d (rkv_id_encode failed)\n", __func__, ids, data->type );
   }
   return true;
   (void)index;
   (void)key;
}

static bool clear_transaction( rkv_private * This, const char * name, utils_map transaction ) {
   if( RKV_DBG ) {
      size_t size;
      if( utils_map_get_size( transaction, &size )) {
         struct timeval tv;
         gettimeofday( &tv, NULL );
         fprintf( stderr, "%6ld.%06ld:DEBUG:rkv_publish:transaction '%s', %ld data sent\n", tv.tv_sec, tv.tv_usec, name, size );
      }
   }
   utils_map_remove( This->transactions, name );
   utils_map_delete( &transaction );
   return true;
}

bool rkv_publish( rkv cache, const char * name ) {
   if( cache == NULL ) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This = (rkv_private *)cache;
   utils_map transaction = NULL;
   return utils_map_get( This->transactions, name, (void **)&transaction )
      &&  net_buff_clear( This->send_buff )
      &&  utils_map_foreach( transaction, rkv_data_encode, This )
      &&  net_buff_flip( This->send_buff )
      &&  net_buff_send( This->send_buff, This->sckt, &This->send_addr )
      &&  clear_transaction( This, name, transaction );
}

static void log_refreshed( utils_map received_data ) {
   if( RKV_DBG ) {
      size_t card;
      utils_map_get_size( received_data, &card );
      struct timeval tv;
      gettimeofday( &tv, NULL );
      if( received_data && utils_map_get_size( received_data, &card )) {
         fprintf( stderr, "%6ld.%06ld:DEBUG:rkv_refresh:%ld data refreshed\n", tv.tv_sec, tv.tv_usec, card );
      }
      else {
         fprintf( stderr, "%6ld.%06ld:DEBUG:rkv_refresh:no data refreshed\n", tv.tv_sec, tv.tv_usec );
      }
   }
}

static bool print_data_address( size_t index, const void * key, const void * value, void * user_context ) {
   fprintf( stderr, "rkv_refresh {key = %p, value = %p} moved from received cache to read_only_cache\n", key, value );
   return true;
   (void)index;
   (void)user_context;
}

bool rkv_refresh( rkv cache ) {
   if( cache == NULL ) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This  = (rkv_private *)cache;
   pthread_mutex_lock( &This->received_data_lock );
   utils_map received_data = This->received_data;
   This->received_data     = NULL;
   pthread_mutex_unlock( &This->received_data_lock );
   log_refreshed( received_data );
   if( received_data ) {
      if( ! utils_map_merge( This->read_only_data, received_data )) {
         return false;
      }
      if( RKV_DBG_MEMORY ) {
         utils_map_foreach( This->read_only_data, print_data_address, NULL );
      }
      if( ! utils_map_delete( &received_data )) {
         return false;
      }
   }
   return true;
}

bool rkv_get( rkv cache, const rkv_id id, const void ** dest ) {
   if(( cache == NULL )||( dest == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This  = (rkv_private *)cache;
   void * entry = NULL;
   if( ! utils_map_get( This->read_only_data, id, &entry )) {
      free( entry );
      return false;
   }
   rkv_data_holder * data = (rkv_data_holder *)entry;
   *dest = data->payload;
   return true;
}

static bool remove_payloads( size_t index, const void * key, const void * value, void * user_context ) {
   const rkv_data_holder * holder = value;
   utils_map               codecs = (utils_map)user_context;
   rkv_codec *             codec  = NULL;
   if(   utils_map_get( codecs, &holder->type, (void **)&codec )
      && codec
      && codec->releaser )
   {
      codec->releaser( CONST_CAST( holder->payload, void), codecs );
   }
   return true;
   (void)index;
   (void)key;
}

static bool delete_transaction( size_t index, const void * key, const void * value, void * user_context ) {
   void * map = CONST_CAST( value, utils_map );
   utils_map_delete((utils_map *)&map );
   return true;
   (void)index;
   (void)key;
   (void)user_context;
}

bool rkv_delete( rkv * cache ) {
   if(( cache == NULL )||( *cache == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This = *(rkv_private **)cache;
   pthread_mutex_lock( &This->received_data_lock );
   This->is_alive = false;
   pthread_mutex_unlock( &This->received_data_lock );
   if( setsockopt( This->sckt, IPPROTO_IP, IP_DROP_MEMBERSHIP, &This->imr, sizeof( This->imr )) < 0 ) {
      perror( "setsockopt( IP_DROP_MEMBERSHIP )" );
      return false;
   }
   pthread_cancel( This->thread );
   void * retVal = NULL;
   pthread_join( This->thread, &retVal );
   close( This->sckt );
   net_buff_delete ( &This->recv_buff );
   net_buff_delete ( &This->send_buff );
   utils_map_foreach( This->read_only_data, remove_payloads, This->codecs );
   utils_map_delete( &This->read_only_data );
   utils_map_foreach( This->transactions, delete_transaction, NULL );
   utils_map_delete( &This->transactions );
   if( This->received_data ) {
      utils_map_delete( &This->received_data );
   }
   utils_map_delete( &This->codecs );
   rkv_listener listener = This->listeners;
   while( listener ) {
      rkv_listener next = listener->next;
      free( listener );
      listener = next;
   }
   free( This );
   *cache = NULL;
   return true;
}
