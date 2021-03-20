#include "all_tests.h"

#include <string.h>

void rkv_test( struct tests_report * report ) {
   tests_chapter( report, "rkv" );
   rkv This = NULL;
   rkv_codec person_codec = {

   };
   ASSERT( rkv_new      ( &This, "239.0.0.66", 2416 );
   ASSERT( rkv_add_codec(  This, person_codec );
   ASSERT( rkv_put      (  This, const rkv_data * data );
   ASSERT( rkv_get      (  This, rkv_data * data );
   ASSERT( rkv_publish  (  This );
   ASSERT( rkv_refresh  (  This );
   ASSERT( rkv_delete   ( &This );
}
