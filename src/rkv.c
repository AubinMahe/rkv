#include <rkv.h>
#include <rkv_id.h>

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
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

// Astuce pour libérer un pointeur const void * en espérant qu'il ne soit pas "vraiment" const,
// sinon c'est SIGSEGV !
// Ne fonctionne que si les pointeurs sont stockables sur 64 bits
#define CONST_CAST(p,T)   ((T *)(uint64_t)(p))
#define MCAST_MIN        9
#define MCAST_MAX       11
#define PAYLOAD_MAX     (64*1024)
#define NET_ID_MAX      (10+1+15)
#define RKV_DBG         true

const rkv_codec rkv_codec_Zero = { 0U, NULL, NULL };

typedef struct {
   rkv_id   id;
   unsigned type;
   void *   data;
   // TODO remplacer le champ suivant par l'attribut "utils_set transaction" dans rkv_private
   // cela permettra de créer éventuellement plusieurs transactions
   bool     published;
} rkv_data;

typedef struct {
   int                   sckt;
   struct sockaddr_in    recv_addr;
   struct sockaddr_in    send_addr;
   struct ip_mreq        imr;
   bool                  is_alive;
   char                  localID[NET_ID_MAX];
   net_buff              recv_buff;
   net_buff              send_buff;
   pthread_t             thread;
   utils_map             codecs;
   utils_map             data;
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
/*
static bool rkv_send( rkv cache, const char * message ) {
   if( cache == NULL ) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This = (rkv_private *)cache;
   if( ! This->is_alive ) {
      fprintf( stderr, "%s: You must call 'join' first.\n", __func__ );
      return false;
   }
   if( RKV_DBG ) {
      fprintf( stderr, "%s|payload <== encode( username='%s', pseudo='%s', message='%s' )\n",
         __func__, This->username, This->pseudo, message );
   }
   return net_buff_clear        ( This->send_buff )
      &&  net_buff_encode_string( This->send_buff, This->username )
      &&  net_buff_encode_string( This->send_buff, This->pseudo   )
      &&  net_buff_encode_string( This->send_buff, message        )
      &&  net_buff_flip         ( This->send_buff )
      &&  net_buff_send         ( This->send_buff, This->sckt, &This->send_addr );
}
*/
static void * rkv_receive_thread( void * arg ) {
   rkv_private * This = (rkv_private *)arg;
   This->is_alive = true;
/*
   rkv_send((rkv)This, JOIN_MSG );
   while( This->is_alive ) {
      net_buff_clear( This->recv_buff );
      if( net_buff_receive( This->recv_buff, This->sckt, &This->recv_addr )) {
         net_buff_flip( This->recv_buff );
         if( RKV_DBG ) {
            size_t limit = 0;
            if( net_buff_get_limit( This->recv_buff, &limit )) {
               fprintf( stderr, "%s|packet received, %ld bytes\n", __func__, limit );
            }
         }
         pid_t remote_pid;
         if( ! net_buff_decode_int( This->recv_buff, &remote_pid )) {
            continue;
         }
         if( RKV_DBG ) {
            fprintf( stderr, "%s|remote pid: %d\n", __func__, remote_pid );
         }
         char ipv4[INET_ADDRSTRLEN];
         if( NULL == inet_ntop( AF_INET, &This->recv_addr.sin_addr, ipv4, sizeof( ipv4 ))) {
            perror( "inet_ntop" );
            continue;
         }
         char remoteID[NET_ID_MAX];
         snprintf( remoteID, sizeof( remoteID ), "%d@%s", remote_pid, ipv4 );
         if( RKV_DBG ) {
            fprintf( stderr, "%s|remoteID: %s\n", __func__, remoteID );
         }
         if( strcmp( remoteID, This->localID )) {
            char message[PAYLOAD_MAX];
            if( ! net_buff_decode_string( This->recv_buff, message, sizeof( message ))) {
               continue;
            }
            if( RKV_DBG ) {
               fprintf( stderr, "%s|message: '%s'\n", __func__, message );
            }
         }
      }
   }
   if( setsockopt( This->sckt, IPPROTO_IP, IP_DROP_MEMBERSHIP, &This->imr, sizeof( This->imr )) < 0 ) {
      perror( "setsockopt( IP_DROP_MEMBERSHIP )" );
      return false;
   }
*/
   This->is_alive = false;
   return NULL;
}

static int codecid_compare( const void * l, const void * r ) {
   const unsigned * const * pl    = (const unsigned * const *)l;
   const unsigned * const * pr    = (const unsigned * const *)r;
   const unsigned *         left  = *pl;
   const unsigned *         right = *pr;
   return (int)( *left - *right );
}

bool rkv_new( rkv * cache, const char * group, unsigned short port ) {
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
   if( ! utils_map_new( &This->codecs, codecid_compare )) {
      close( This->sckt );
      net_buff_delete( &This->recv_buff );
      net_buff_delete( &This->send_buff );
      free( This );
      return false;
   }
   if( ! utils_map_new( &This->data, rkv_id_compare )) {
      close( This->sckt );
      net_buff_delete( &This->recv_buff );
      net_buff_delete( &This->send_buff );
      free( This );
      return false;
   }
   if( pthread_create( &This->thread, NULL, rkv_receive_thread, This )) {
      perror( "pthread_create" );
      close( This->sckt );
      net_buff_delete ( &This->recv_buff );
      net_buff_delete ( &This->send_buff );
      utils_map_delete( &This->codecs, false );
      utils_map_delete( &This->data  , false );
      free( This );
      return false;
   }
   *cache = (rkv)This;
   return true;
}

bool rkv_add_codec( rkv cache, const rkv_codec * codec ) {
   if(( cache == NULL )||( codec == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This  = (rkv_private *)cache;
   rkv_codec   * value = malloc( sizeof( rkv_codec ));
   if( value == NULL ) {
      perror( "rkv_add_codec/malloc" );
      return false;
   }
   *value = *codec;
   if( ! utils_map_put( This->codecs, &value->type, value )) {
      free( value );
      return false;
   }
   return true;
}

bool rkv_put( rkv cache, const rkv_id * id, unsigned type, void * data ) {
   if(( cache == NULL )||( id == NULL )||( data == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This  = (rkv_private *)cache;
   rkv_data *    entry = malloc( sizeof( rkv_data ));
   entry->id        = *id;
   entry->type      = type;
   entry->data      = data;
   entry->published = false;
   if( ! utils_map_put( This->data, &entry->id, entry )) {
      free( entry );
      return false;
   }
   return true;
}

static bool encode_new_and_updated( size_t index, const void * key, const void * value, void * user_context ) {
   (void)index;
   (void)key;
   rkv_private * This = (rkv_private *)user_context;
   const rkv_data * data  = value;
   if( ! data->published ) {
      rkv_codec * codec = NULL;
      if(   utils_map_get( This->codecs, &data->type, (void **)&codec )
         && codec->encoder( This->send_buff, value, This )   )
      {
         // stratégie optimiste: on anticipe sur l'envoi effectif du message
         CONST_CAST( data, rkv_data )->published = true;
      }
      else {
         fprintf( stderr, "%s: unable to encode data %u.%d@%08lx of type %d\n", __func__,
            data->id.instance, data->id.process, data->id.host, data->type );
      }
   }
   return true;
}

bool rkv_publish( rkv cache ) {
   if( cache == NULL ) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This  = (rkv_private *)cache;
   return net_buff_clear( This->send_buff )
      &&  utils_map_foreach( This->data, encode_new_and_updated, This )
      &&  net_buff_flip( This->send_buff )
      &&  net_buff_send( This->send_buff, This->sckt, &This->send_addr );
}

static bool decode_received( rkv_private * This ) {
   rkv_id id;
   while( rkv_id_decode( &id, This->recv_buff )) {
      unsigned type;
      rkv_codec * codec = NULL;
      rkv_data *  data  = NULL;
      if(   net_buff_decode_uint32( This->recv_buff, &type )
         && utils_map_get( This->codecs, &type, (void **)&codec )
         && codec->decoder( &data, This->recv_buff, This )
         && utils_map_put( This->data, &id, data ))
      {
         ;
      }
      else {
         fprintf( stderr, "%s: unable to decode data %u.%d@%08lx of type %d", __func__, id.instance, id.process, id.host, type );
      }
   }
   return true;
}

bool rkv_refresh( rkv cache ) {
   if( cache == NULL ) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This  = (rkv_private *)cache;
   return net_buff_clear( This->recv_buff )
      &&  net_buff_receive( This->recv_buff, This->sckt, &This->recv_addr )
      &&  net_buff_flip( This->recv_buff )
      &&  decode_received( This );
}

bool rkv_get( rkv cache, const rkv_id * id, void ** dest ) {
   if(( cache == NULL )||( dest == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This  = (rkv_private *)cache;
   void * entry = NULL;
   if( ! utils_map_get( This->data, id, &entry )) {
      free( entry );
      return false;
   }
   rkv_data * data = (rkv_data *)entry;
   *dest = data->data;
   return true;
}

bool rkv_delete( rkv * cache ) {
   if(( cache == NULL )||( *cache == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This = *(rkv_private **)cache;
   This->is_alive = false;
   close( This->sckt );
   void * retVal = NULL;
   pthread_join( This->thread, &retVal );
   net_buff_delete ( &This->recv_buff );
   net_buff_delete ( &This->send_buff );
   utils_map_delete( &This->codecs, false );
   utils_map_delete( &This->data  , false );
   free( This );
   *cache = NULL;
   return true;
}
