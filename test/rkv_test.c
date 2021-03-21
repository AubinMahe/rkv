#include "all_tests.h"
#include <rkv.h>

#include <string.h>

typedef struct {
   unsigned char  day;
   unsigned char  month;
   unsigned short year;
} date;

#define DATE_TYPE_ID 1

typedef struct {
   char forname[20];
   char name[20];
   date birthdate;
} person;

#define PERSON_TYPE_ID 2

static bool person_encode( net_buff buffer, const void * src, void * user_context )  {
   (void)buffer;
   (void)src;
   (void)user_context;
   return false;
}

static bool person_decode( void * dest, net_buff buffer, void * user_context ) {
   (void)dest;
   (void)buffer;
   (void)user_context;
   return false;
}

void rkv_test( struct tests_report * report ) {
   tests_chapter( report, "rkv" );
   rkv This = NULL;
   rkv_codec person_codec = {
      PERSON_TYPE_ID,
      person_encode,
      person_decode
   };
//   person eve      = { "Eve"   , "Mahé", { 28, 2, 2008 }};
//   person muriel   = { "Muriel", "Mahé", { 26, 1, 1973 }};
   person aubin    = { "Aubin" , "Mahé", { 24, 1, 1966 }};
   rkv_id aubin_id = rkv_id_Zero;
   rkv_id_init( &aubin_id, 1 );
   ASSERT( report, rkv_new( &This, "239.0.0.66", 2416 ));
   ASSERT( report, rkv_add_codec(  This, &person_codec ));
   ASSERT( report, rkv_put(  This, &aubin_id, PERSON_TYPE_ID, &aubin ));
   void * data = NULL;
   ASSERT( report, rkv_get(  This, &aubin_id, &data ));
   person * prsn = (person *)data;
   ASSERT( report, 0 == strcmp( prsn->forname, "Aubin" ));
   ASSERT( report, 0 == strcmp( prsn->name   , "Mahé"  ));
   ASSERT( report, prsn->birthdate.day   ==   24 );
   ASSERT( report, prsn->birthdate.month ==    1 );
   ASSERT( report, prsn->birthdate.year  == 1966 );
   ASSERT( report, rkv_publish( This ));
   ASSERT( report, rkv_refresh( This ));
   ASSERT( report, rkv_delete( &This ));
}
