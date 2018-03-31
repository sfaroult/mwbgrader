#ifndef _DBOP_H

#define _DBOP_H

#include "schema.h"

typedef struct relationship {
          char *tab1;
          char *cardinality;
          char *tab2;
          struct relationship *next;
        } RELATIONSHIP_T;

typedef struct name_item {
          char *name;
          struct name_item *next;
        } NAME_ITEM_T;

extern short insert_variant(char *model);
extern short insert_variant_test(short varid, int testid, char pass);

extern int   insert_table(TABTABLE_T *t);
extern int   insert_column(TABCOLUMN_T *c);
extern int   insert_index(TABINDEX_T *i);
extern int   insert_indexcol(TABINDEXCOL_T *ic);
extern int   insert_foreignkey(TABFOREIGNKEY_T *fk, COLFOREIGNKEY_T *cols);

extern void  table_figure(char *mwbid);
extern void  only_figures(void);

extern int   db_begin_tx(void);
extern int   db_commit(void);
extern int   db_rollback(void);
extern int   db_connect(void);
extern int   db_refconnect(char *dbname);
extern void  db_disconnect(void);

extern int   db_runcheck(short  check_code,
                         char   report,
                         char  *query,
                         int    varid);
extern int   db_refcheck(short check_code, char *query, int varid);

extern RELATIONSHIP_T *db_relationships(void);
extern NAME_ITEM_T    *db_selfref(void);
extern int             db_basic_info(int *ptabcnt, int *pdatacnt);
extern short           best_variant(int *pbase_grade, int *pdpct, int *ptpct);
extern int             schema_matching(short varid);
extern void            get_variant_tests(short  varid,
                                         short *checks,
                                         int    check_cnt);
extern void            free_relationships(RELATIONSHIP_T **rp);
extern void            free_names(NAME_ITEM_T **np);

#endif
