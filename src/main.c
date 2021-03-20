#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rkv.h"
#include <utils/utils_cli.h>

static bool add_entry( rkv cache ) {
   (void)cache;
   return false;
}

static bool display( rkv cache ) {
   (void)cache;
   return false;
}

int main( int argc, char * argv[] ) {
   int ret = EXIT_FAILURE;
   const char *   group = NULL;
   unsigned short port  = 0;
   utils_cli_arg  cli   = NULL;
   if( utils_cli_new( &cli, argc, argv,
      "group", utils_cli_STRING, true ,   NULL, &group,
      "port" , utils_cli_USHORT, false, "2416", &port,
      NULL ))
   {
      if( port >= 1024 ) {
         rkv cache = NULL;
         if( rkv_new( &cache, group, port )) {
            while( add_entry( cache )) {
               display( cache );
            }
            ret = EXIT_SUCCESS;
         }
      }
   }
   return ret;
}
