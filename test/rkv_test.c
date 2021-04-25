#include "all_tests.h"
#include <rkv.h>
#include <utils/utils_time.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
   unsigned char  day;
   unsigned char  month;
   unsigned short year;
} date;

#define RKV_DBG_MEMORY false

static const unsigned DATE_TYPE_ID = 1;

static bool date_encode( net_buff buffer, const void * src, utils_map codecs ) {
   const date * d = (const date *)src;
   (void)codecs;
   return net_buff_encode_byte  ( buffer, d->day )
      &&  net_buff_encode_byte  ( buffer, d->month )
      &&  net_buff_encode_uint16( buffer, d->year );
}

static bool date_decode( void * dest, net_buff buffer, utils_map codecs ) {
   if( dest == NULL ) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   date d;
   if(   net_buff_decode_byte  ( buffer, &d.day   )
      && net_buff_decode_byte  ( buffer, &d.month )
      && net_buff_decode_uint16( buffer, &d.year  ))
   {
      date * p = *(date **)dest;
      if( p == NULL ) {
         p = malloc( sizeof( date ));
         if( p == NULL ) {
            perror( "malloc" );
            return false;
         }
         if( RKV_DBG_MEMORY ) {
            fprintf( stderr, "%s: %p = malloc( sizeof( date ))\n", __func__, (void *)p );
         }
         *((date **)dest) = p;
      }
      *p = d;
      return true;
   }
   return false;
   (void)codecs;
}

static void date_releaser( void * data, utils_map codecs ) {
   if( RKV_DBG_MEMORY ) {
      fprintf( stderr, "%s: free( %p )\n", __func__, data );
   }
   free( data );
   (void)codecs;
}

static int date_compare( const date * left, const date * right ) {
   if(( left == NULL )&&( right == NULL )) {
      return 0;
   }
   if( left &&( right == NULL )) {
      return +1;
   }
   if(( left == NULL )&& right ) {
      return -1;
   }
   int diff = left->year - right->year;
   if( diff == 0 ) {
      diff = left->month - right->month;
      if( diff == 0 ) {
         diff = left->day - right->day;
      }
   }
   return diff;
}

typedef struct {
   char forname[20];
   char name[20];
   date birthday;
} person;

static const unsigned PERSON_TYPE_ID = 2;

static bool person_encode( net_buff buffer, const void * src, utils_map codecs ) {
   const person * p = (const person *)src;
   rkv_codec * date_codec = NULL;
   return net_buff_encode_string( buffer, p->forname )
      &&  net_buff_encode_string( buffer, p->name )
      &&  utils_map_get( codecs, &DATE_TYPE_ID, (map_value *)&date_codec )
      &&  date_codec
      &&  date_codec->encoder( buffer, &p->birthday, codecs );
}

static bool person_decode( void * dest, net_buff buffer, utils_map codecs ) {
   person ** ppp = (person **)dest;
   if( ppp == NULL ) {
      fprintf( stderr, "%s: null argument\n", __func__ );
      return false;
   }
   person      p;
   date *      birthday   = &p.birthday;
   rkv_codec * date_codec = NULL;
   if(   net_buff_decode_string( buffer, p.forname, sizeof( p.forname ))
      && net_buff_decode_string( buffer, p.name   , sizeof( p.name ))
      && utils_map_get( codecs, &DATE_TYPE_ID, (map_value *)&date_codec )
      && date_codec
      && date_codec->factory( &birthday, buffer, codecs ))
   {
      person * pp = *ppp;
      if( pp == NULL ) {
         pp = malloc( sizeof( person ));
         if( pp == NULL ) {
            perror( "malloc" );
            return false;
         }
         if( RKV_DBG_MEMORY ) {
            fprintf( stderr, "%s: %p = malloc( sizeof( person ))\n", __func__, (void *)pp );
         }
         *ppp = pp;
      }
      *pp = p;
      return true;
   }
   return false;
}

static void person_releaser( void * data, utils_map codecs ) {
   if( RKV_DBG_MEMORY ) {
      fprintf( stderr, "%s: free( %p )\n", __func__, data );
   }
   free( data );
   (void)codecs;
}

static int person_compare( const person * left, const person * right ) {
   if(( left == NULL )&&( right == NULL )) {
      return 0;
   }
   if( left &&( right == NULL )) {
      return +1;
   }
   if(( left == NULL )&& right ) {
      return -1;
   }
   int diff = strcmp( left->name, right->name );
   if( diff == 0 ) {
      diff = strcmp( left->forname, right->forname );
      if( diff == 0 ) {
         diff = date_compare( &left->birthday, &right->birthday );
      }
   }
   return diff;
}

static rkv_id eve_id      = NULL;
static rkv_id muriel_id   = NULL;
static rkv_id aubin_id    = NULL;
static rkv_id aubin_bd_id = NULL;

static const person eve      = { "Eve"   , "Mahé", { 28, 2, 2008 }};
static const person muriel   = { "Muriel", "Mahé", { 26, 1, 1973 }};
static const person aubin    = { "Aubin" , "Mahé", { 24, 1, 1966 }};
static const date   aubin_bd = aubin.birthday;

static pthread_cond_t  on_receive_ended       = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t on_receive_ended_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool            receive_ended          = false;

static void on_receive( rkv This, void * user_context ) {
   struct tests_report * report = (struct tests_report *)user_context;
   tests_chapter( report, "rkv notification" );
   ASSERT( report, rkv_refresh( This ));
   const void * data = NULL;
   ASSERT( report, rkv_get(  This, eve_id     , &data ));
   ASSERT( report, person_compare((const person *)data, &eve    ) == 0  );
   ASSERT( report, rkv_get(  This, muriel_id  , &data ));
   ASSERT( report, person_compare((const person *)data, &muriel ) == 0  );
   ASSERT( report, rkv_get(  This, aubin_id   , &data ));
   ASSERT( report, person_compare((const person *)data, &aubin  ) == 0  );
   ASSERT( report, rkv_get(  This, aubin_bd_id, &data ));
   ASSERT( report, date_compare((const date *)data, &aubin_bd ) == 0  );
   pthread_mutex_lock( &on_receive_ended_mutex );
   receive_ended = true;
   pthread_cond_signal( &on_receive_ended );
   pthread_mutex_unlock( &on_receive_ended_mutex );
}

static bool dump( size_t index, rkv_id id, unsigned type, const void * data, void * user_context ) {
   struct tests_report * report = (struct tests_report *)user_context;
   if( type == PERSON_TYPE_ID ) {
      switch( index ) {
      case 0:
         ASSERT( report, rkv_id_compare( &id, &eve_id ) == 0 );
         ASSERT( report, person_compare( data, &eve ) == 0 );
         break;
      case 1:
         ASSERT( report, rkv_id_compare( &id, &muriel_id ) == 0 );
         ASSERT( report, person_compare( data, &muriel ) == 0 );
         break;
      case 2:
         ASSERT( report, rkv_id_compare( &id, &aubin_id ) == 0 );
         ASSERT( report, person_compare( data, &aubin ) == 0 );
         break;
      default:
         ASSERT( report, false );
         break;
      }
   }
   else if( type == DATE_TYPE_ID ) {
      ASSERT( report, rkv_id_compare( &id, &aubin_bd_id ) == 0 );
      ASSERT( report, date_compare( data, &aubin_bd ) == 0 );
   }
   else {
      ASSERT( report, false );
   }
   return true;
}

void rkv_test( struct tests_report * report ) {
   const char * trnsctn_name = "Ma transaction";
   rkv This = NULL;
   rkv_codec date_codec = {
      DATE_TYPE_ID,
      date_encode,
      date_decode,
      date_releaser
   };
   rkv_codec person_codec = {
      PERSON_TYPE_ID,
      person_encode,
      person_decode,
      person_releaser
   };
   tests_chapter( report, "rkv id new" );
   ASSERT( report, rkv_id_new( &eve_id ));
   ASSERT( report, rkv_id_new( &muriel_id ));
   ASSERT( report, rkv_id_new( &aubin_id ));
   ASSERT( report, rkv_id_new( &aubin_bd_id ));
   const rkv_codec * const codecs[] = {
      &person_codec,
      &date_codec
   };

   tests_chapter( report, "rkv new and listener" );
   ASSERT( report, rkv_new( &This, "239.0.0.66", 2416, codecs, sizeof(codecs)/sizeof(codecs[0] )));
   ASSERT( report, rkv_add_listener( This, on_receive, report ));

   tests_chapter( report, "rkv put and publish" );
   ASSERT( report, rkv_put( This, trnsctn_name, eve_id     , PERSON_TYPE_ID, &eve ));
   ASSERT( report, rkv_put( This, trnsctn_name, muriel_id  , PERSON_TYPE_ID, &muriel ));
   ASSERT( report, rkv_put( This, trnsctn_name, aubin_id   , PERSON_TYPE_ID, &aubin ));
   ASSERT( report, rkv_put( This, trnsctn_name, aubin_bd_id, DATE_TYPE_ID  , &aubin_bd ));
   receive_ended = false;
   ASSERT( report, rkv_publish( This, trnsctn_name ));
   pthread_mutex_lock( &on_receive_ended_mutex );
   while( ! receive_ended ) {
      pthread_cond_wait( &on_receive_ended, &on_receive_ended_mutex );
   }
   pthread_mutex_unlock( &on_receive_ended_mutex );

   tests_chapter( report, "rkv foreach" );
   rkv_foreach( This, dump, report );

   tests_chapter( report, "rkv delete" );
   ASSERT( report, rkv_delete( &This ));
   ASSERT( report, rkv_id_delete( &eve_id ));
   ASSERT( report, rkv_id_delete( &muriel_id ));
   ASSERT( report, rkv_id_delete( &aubin_id ));
   ASSERT( report, rkv_id_delete( &aubin_bd_id ));
}
