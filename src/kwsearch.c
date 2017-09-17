/*
 *  Routines for searching keywords in ordered arrays
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "kwsearch.h"

extern int kw_search(int kwc, char *kwv[], char *w) {
  int   start = 0;
  int   end = kwc - 1;
  int   mid;
  int   pos = -1;
  int   comp;

  if (kwv && kwc && w) {
    //fprintf(stderr, "Looking for %s\n", w);
    while (start <= end){
      mid = (start + end) / 2;
      /*
      fprintf(stderr, "Comparing %s@%d [%s@%d to %s@%d] to %s\n",
              kwv[mid], mid,
              kwv[start], start,
              kwv[end], end,
              w);
     */
      //                      s1      s2
      if ((comp = strcasecmp(kwv[mid], w)) == 0) {
         pos = mid;
         start = end + 1; // Found
      } else {
        //fprintf(stderr, "comp = %d start = %d, end = %d\n", comp, start, end);
        if (comp < 0) {  // Searched word comes after word @mid
           // s1 < s2
           start = mid + 1;
        } else {
           // s1 > s2
           end = mid - 1;
        }
        //fprintf(stderr, "comp = %d start = %d (%s), end = %d (%s)\n",
        //        comp, start, kwv[start], end, kwv[end]);
      }
    }
  }
  return pos;
}

extern char *kw_value(int kwc, char *kwv[], int code) {
  if ((code >= 0) && (code < kwc)) {
    return kwv[code];
  } else {
    return (char *)NULL;
  }
}

