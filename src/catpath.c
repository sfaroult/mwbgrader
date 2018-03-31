#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "kwsearch.h"
#include "catpath.h"

static char *G_catpath_words[] = {
    "DIAGRAM",
    "DIAGRAM_FOREIGNKEY",
    "DIAGRAM_TABLEFIGURE",
    "DIAGRAM_TABLEFIGURE_DIAGRAM",
    "DIAGRAM_TABLEFIGURE_TABLE",
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
  return kw_search(CATPATH_KWCOUNT, G_catpath_words, w, "catpath");
}

extern char *catpath_keyword(int code) {
  return kw_value(CATPATH_KWCOUNT, G_catpath_words, code);
}

