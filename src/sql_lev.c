/*
 *  Written by S Faroult
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sqlite3.h>

extern int levenshtein(char *s1, char *s2);

/*
 *   Returns the levenshtein distance.
 */
extern void sql_lev(sqlite3_context  *context,
                    int               argc,
                    sqlite3_value   **argv){
  int    dist;
  char  *s1;
  char  *s2;

  // Check whether the value is actually a number
  s1 = (char *)sqlite3_value_text(argv[0]);
  s2 = (char *)sqlite3_value_text(argv[1]);
  if ((s1 == NULL) || (s2 == NULL)) {
     sqlite3_result_null(context);
  } else {
     dist = levenshtein(s1, s2);
     sqlite3_result_int(context, dist);
  }
}
