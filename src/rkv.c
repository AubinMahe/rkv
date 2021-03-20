#include "rkv.h"

#include <net/net_buff.h>

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

#define MCAST_MIN        9
#define MCAST_MAX       11
#define PAYLOAD_MAX     (64*1024)
#define NET_ID_MAX      (10+1+15)
#define RKV_DBG         true

const rkv_codec rkv_codec_Zero = { 0U, NULL, NULL };

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
static bool rkv_send( rkv ac, const char * message ) {
   if( ac == NULL ) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This = (rkv_private *)ac;
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

bool rkv_new( rkv * cache, const char * group, unsigned short port ) {
   if(( cache == NULL )||( group == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   *cache = malloc( sizeof( rkv_private ));
   if( *cache == NULL ) {
      return false;
   }
   rkv_private * This = (rkv_private *)*cache;
   memset( This, 0, sizeof( rkv_private ));
#ifdef _WIN32
   WSADATA wsaData;
   if( WSAStartup( 0x0101, &wsaData )) {
      perror( "WSAStartup" );
      return false;
   }
#endif
   This->sckt     = -1;
   if( ! net_buff_new( &This->recv_buff, PAYLOAD_MAX )) {
      free( This );
      *cache = NULL;
      return false;
   }
   if( ! net_buff_new( &This->send_buff, PAYLOAD_MAX )) {
      net_buff_delete( &This->recv_buff );
      free( This );
      *cache = NULL;
      return false;
   }
   if( strlen( group ) < MCAST_MIN ) {
      fprintf( stderr, "%s: multicast IP v4 address too short: %s, expected 239.0.0.[0..255]\n", __func__, group );
      return false;
   }
   if( strlen( group ) > MCAST_MAX ) {
      fprintf( stderr, "%s: multicast IP v4 address too long: %s, expected 239.0.0.[0..255]\n", __func__, group );
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
      return false;
   }
   unsigned yes = 1;
   if( setsockopt( This->sckt, SOL_SOCKET, SO_REUSEADDR, (char*) &yes, sizeof( yes )) < 0 ) {
      perror( "setsockopt( SOL_SOCKET, SO_REUSEADDR )" );
      return false;
   }
   if( bind( This->sckt, (struct sockaddr*) &This->recv_addr, sizeof( This->recv_addr )) < 0 ) {
      perror( "bind" );
      return false;
   }
   if( setsockopt( This->sckt, IPPROTO_IP, IP_ADD_MEMBERSHIP, &This->imr, sizeof( This->imr )) < 0 ) {
      perror( "setsockopt( IP_ADD_MEMBERSHIP )" );
      return false;
   }
   const pid_t    pid    = getpid();
   const long int hostid = gethostid();
   char           ipv4[INET_ADDRSTRLEN];
   get_multicast_interface_address( ipv4 );
   snprintf( This->localID, sizeof( This->localID ), "%d/%ld@%s:%d", pid, hostid, ipv4, This->recv_addr.sin_port );
   This->is_alive = false;
   if( pthread_create( &This->thread, NULL, rkv_receive_thread, This )) {
      perror( "pthread_create" );
      return false;
   }
   return true;
}

bool rkv_delete( rkv * ac ) {
   if(( ac == NULL )||( *ac == NULL )) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   rkv_private * This = *(rkv_private **)ac;
   if( This->is_alive ) {
      This->is_alive = false;
      close( This->sckt );
      void * retVal = NULL;
      pthread_join( This->thread, &retVal );
   }
   net_buff_delete( &This->recv_buff );
   net_buff_delete( &This->send_buff );
   free( *ac );
   *ac = NULL;
   return true;
}
