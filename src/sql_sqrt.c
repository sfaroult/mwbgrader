/*
 *  Written by S Faroult
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sqlite3.h>
#include <math.h>

extern void sql_sqrt(sqlite3_context  *context,
                     int               argc,
                     sqlite3_value   **argv){
  double  val;
  char   *v;

  // Check whether the value is actually a number
  v = (char *)sqlite3_value_text(argv[0]);
  if (v == NULL){
     sqlite3_result_null(context);
  } else {
     if (sscanf(v, "%lf", &val) == 1) {
        // Do things well, use the provided function
        val = sqlite3_value_double(argv[0]);
        sqlite3_result_double(context, sqrt(val));
     } else {
        // Wrong input
        sqlite3_result_error(context, "Invalid numerical value", -1);
     }
  }
}
