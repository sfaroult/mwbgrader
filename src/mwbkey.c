#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "kwsearch.h"
#include "mwbkey.h"

static char *G_mwbkey_words[] = {
    "AUTOINCREMENT",
    "AVGROWLENGTH",
    "CHARACTERSETNAME",
    "CHECKSUM",
    "COLLATIONNAME",
    "COLUMNLENGTH",
    "COLUMNS",
    "COMMENT",
    "COMMENTEDOUT",
    "CONNECTIONSTRING",
    "CREATEDATE",
    "DATATYPEEXPLICITPARAMS",
    "DEFAULTCHARACTERSETNAME",
    "DEFAULTCOLLATIONNAME",
    "DEFAULTVALUE",
    "DEFAULTVALUEISNULL",
    "DEFERABILITY",
    "DELAYKEYWRITE",
    "DELETERULE",
    "DESCEND",
    "FOREIGNKEY",
    "FOREIGNKEYS",
    "INDEX",
    "INDEXKIND",
    "INDEXTYPE",
    "INDICES",
    "ISNOTNULL",
    "ISPRIMARY",
    "ISSTUB",
    "ISSYSTEM",
    "ISTEMPORARY",
    "KEYBLOCKSIZE",
    "LASTCHANGEDATE",
    "LENGTH",
    "MANDATORY",
    "MANY",
    "MAXROWS",
    "MERGEINSERT",
    "MERGEUNION",
    "MINROWS",
    "MODELONLY",
    "NAME",
    "NEXTAUTOINC",
    "OLDNAME",
    "OWNER",
    "PACKKEYS",
    "PARTITIONCOUNT",
    "PARTITIONEXPRESSION",
    "PARTITIONTYPE",
    "PASSWORD",
    "PRECISION",
    "PRIMARYKEY",
    "RAIDCHUNKS",
    "RAIDCHUNKSIZE",
    "RAIDTYPE",
    "REFERENCEDCOLUMN",
    "REFERENCEDCOLUMNS",
    "REFERENCEDMANDATORY",
    "REFERENCEDTABLE",
    "ROWFORMAT",
    "SCALE",
    "SIMPLETYPE",
    "SUBPARTITIONCOUNT",
    "SUBPARTITIONEXPRESSION",
    "SUBPARTITIONTYPE",
    "TABLE",
    "TABLEDATADIR",
    "TABLEENGINE",
    "TABLEINDEXDIR",
    "TEMPORARYSCOPE",
    "TEMP_SQL",
    "UNIQUE",
    "UPDATERULE",
    "WITHPARSER",
    NULL};

extern int mwbkey_search(char *w) {
  return kw_search(MWBKEY_COUNT, G_mwbkey_words, w);
}

extern char *mwbkey_keyword(int code) {
  return kw_value(MWBKEY_COUNT, G_mwbkey_words, code);
}

