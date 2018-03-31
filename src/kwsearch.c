/*
 *  Routines for searching keywords in ordered arrays
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "kwsearch.h"

//#define DEBUG

extern int kw_search(int kwc, char *kwv[], char *w, char * label) {
  int   start = 0;
  int   end = kwc - 1;
  int   mid;
  int   pos = -1;
  int   comp;

  if (kwv && kwc && w) {
#ifdef DEBUG
    fprintf(stderr, "\n[%s] Looking for %s\n", label, w);
#endif
    while (start <= end){
      mid = (start + end) / 2;
#ifdef DEBUG
      fprintf(stderr, "[%s] Comparing %s@%d [%s@%d to %s@%d] to %s\n",
            label,
            kwv[mid], mid,
            kwv[start], start,
            kwv[end], end,
            w);
#endif
      //                      s1      s2
      if ((comp = strcasecmp(kwv[mid], w)) == 0) {
        pos = mid;
        start = end + 1; // Found
      } else {
#ifdef DEBUG
        fprintf(stderr, "[%s] comp = %d start = %d, end = %d\n",
                label, comp, start, end);
#endif
        if (comp < 0) {  // Searched word comes after word @mid
          // s1 < s2
          start = mid + 1;
        } else {
          // s1 > s2
          end = mid - 1;
        }
#ifdef DEBUG
        fprintf(stderr, "[%s] comp = %d start = %d (%s), end = %d (%s)\n",
                label, comp, start, kwv[start], end, kwv[end]);
#endif
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

