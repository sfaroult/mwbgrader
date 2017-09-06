#ifndef _SCHEMA_H

#define _SCHEMA_H

#define ID_LEN      50
#define NAME_LEN    64
#define TYPE_LEN    20

typedef struct tabtable {
            short varid;
            char  id[ID_LEN];
            char  name[NAME_LEN];
            short comment_len;
          } TABTABLE_T;

typedef struct tabcolumn {
            short varid;
            char  id[ID_LEN];
            char  tabid[ID_LEN];
            char  name[NAME_LEN];
            short comment_len;
            char  datatype[TYPE_LEN];
            char  autoinc;
            char *defaultvalue;
            char  isnotnull;
            short collength;
            short precision;
            short scale;
          } TABCOLUMN_T;

typedef struct tabindex {
            short varid;
            char  id[ID_LEN];
            char  tabid[ID_LEN];
            char  name[NAME_LEN];
            char  isprimary;
            char  isunique;
          } TABINDEX_T;

typedef struct tabindexcol {
            short varid;
            char  tabid[ID_LEN];
            char  idxid[ID_LEN];
            char  colid[ID_LEN];
            short seq;
          } TABINDEXCOL_T;

typedef struct tabforeignkey {
            short varid;
            char  id[ID_LEN];
            char  tabid[ID_LEN];
            char  name[NAME_LEN];
            char  reftabid[ID_LEN];
          } TABFOREIGNKEY_T;

typedef struct colforeignkey {
            short varid;
            char  colid[ID_LEN];
            char  refcolid[ID_LEN];
            struct colforeignkey *next;
          } COLFOREIGNKEY_T;

extern void free_colfk(COLFOREIGNKEY_T **colfkp);
extern void add_colfk(COLFOREIGNKEY_T **listp, char *id);
extern void add_refcolfk(COLFOREIGNKEY_T **listp, char *id);

#endif
