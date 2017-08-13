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
  char *wup;
  char *p;

  if (kwv && kwc && w) {
    // strcasecmp() seems to have problems with _.
    // Converting the word to uppercase and performing
    // a plain case sensitive comparison
    wup = strdup(w);
    assert(wup);
    p = wup;
    while (*p) {
      *p = toupper(*p);
      p++;
    }
    while (start <= end){
      mid = (start + end) / 2;
      /*
      printf("Comparing %s@%d [%s@%d to %s@%d] to %s\n",
              kwv[mid], mid,
              kwv[start], start,
              kwv[end], end,
              w);
      */
      if ((comp = strcmp(kwv[mid], wup)) == 0) {
         pos = mid;
         start = end + 1; // Found
      } else {
        if (comp < 0) {  // Searched word comes after word @mid
           start = mid + 1;
        } else {
           end = mid - 1;
        }
      }
      // printf("comp = %d start = %d, end = %d\n", comp, start, end);
    }
    free(wup);
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

