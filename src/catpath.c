#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "kwsearch.h"
#include "catpath.h"

static char *G_catpath_words[] = {
    "FOREIGNKEY",
    "TABLE",
    "TABLE_COLUMN",
    "TABLE_FOREIGNKEY",
    "TABLE_FOREIGNKEY_INDEX",
    "TABLE_FOREIGNKEY_TABLE",
    "TABLE_INDEX",
    "TABLE_INDEX_INDEXCOLUMN",
    "TABLE_INDEX_INDEXCOLUMN_COLUMN",
    NULL};

extern int catpath_search(char *w) {
  return kw_search(CATPATH_COUNT, G_catpath_words, w);
}

extern char *catpath_keyword(int code) {
  return kw_value(CATPATH_COUNT, G_catpath_words, code);
}

