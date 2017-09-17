#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "kwsearch.h"
#include "catpath.h"

static char *G_catpath_words[] = {
    "FOREIGNKEY",
    "TABLE",
    "TABLE|COLUMN",
    "TABLE|FOREIGNKEY",
    "TABLE|FOREIGNKEY|INDEX",
    "TABLE|FOREIGNKEY|TABLE",
    "TABLE|INDEX",
    "TABLE|INDEX|INDEXCOLUMN",
    "TABLE|INDEX|INDEXCOLUMN|COLUMN",
    NULL};

extern int catpath_search(char *w) {
  return kw_search(CATPATH_COUNT, G_catpath_words, w, "catpath");
}

extern char *catpath_keyword(int code) {
  return kw_value(CATPATH_COUNT, G_catpath_words, code);
}

