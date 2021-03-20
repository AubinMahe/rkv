#include "all_tests.h"

#include <stdlib.h>

int main( int argc, char * argv[] ) {
   return tests_run( argc, argv,
      "rkv_test", rkv_test,
      NULL );
}
