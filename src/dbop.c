#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sqlite3.h>

static sqlite3 *G_db = NULL;

#include "schema.h"
#include "grad.h"
#include "dbop.h"
#include "debug.h"

#define _must_succeed(what, f)   {int ret_code; \
                                  if ((ret_code = f) != SQLITE_OK) { \
                                    fprintf(stderr, "%s: %s\n", \
                                            what, \
                                            sqlite3_errstr(ret_code)); \
                                    return 1;}} 

#define _check_code(x, expected)   {if (x != expected) { \
                                      fprintf(stderr, "%s\n", \
                                              sqlite3_errstr(x)); \
                                      return 1;}} 

static RELATIONSHIP_T *new_relationship(char *t1, char *c, char *t2) {
    RELATIONSHIP_T *r = NULL;

    if (t1 && c && t2) {
      if ((r = (RELATIONSHIP_T *)malloc(sizeof(RELATIONSHIP_T))) != NULL) {
        r->tab1 = strdup(t1);
        r->cardinality = strdup(c);
        r->tab2 = strdup(t2);
        r->next = NULL;
      }
    }
    return r;
}

static void add_relationship(RELATIONSHIP_T **listp, RELATIONSHIP_T *r) {
    if (listp && r) {
      if (*listp == NULL) {
        *listp = r;
      } else {
        add_relationship(&((*listp)->next), r);
      }
    }
}

extern void free_relationships(RELATIONSHIP_T **rp) {
    if (rp && *rp) {
      free_relationships(&((*rp)->next));
      if ((*rp)->tab1) {
        free((*rp)->tab1);
      }
      if ((*rp)->cardinality) {
        free((*rp)->cardinality);
      }
      if ((*rp)->tab2) {
        free((*rp)->tab2);
      }
      free(*rp);
      *rp = NULL;
    }
}

static NAME_ITEM_T *new_name(char *n) {
    NAME_ITEM_T *ni = NULL;

    if (n) {
      if ((ni = (NAME_ITEM_T *)malloc(sizeof(NAME_ITEM_T))) != NULL) {
        ni->name = strdup(n);
        ni->next = NULL;
      }
    }
    return ni;
}

static void add_name(NAME_ITEM_T **listp, NAME_ITEM_T *n) {
    if (listp && n) {
      if (*listp == NULL) {
        *listp = n;
      } else {
        add_name(&((*listp)->next), n);
      }
    }
}

extern void free_names(NAME_ITEM_T **np) {
    if (np && *np) {
      free_names(&((*np)->next));
      if ((*np)->name) {
        free((*np)->name);
      }
      free(*np);
      *np = NULL;
    }
}

// In-memory table creation
static char *G_ddl[] =
            {"create table tabTable"
             "   (id          varchar(50) not null primary key,"
             "    name        varchar(64) not null,"
             "    comment_len int default 0,"
             "    constraint tabTable_u"
             "       unique (name))",
             "create table tabColumn"
             "   (id            varchar(50) not null primary key,"
             "    tabid         varchar(50) not null,"
             "    name          varchar(64) not null,"
             "    dataType      varchar(20) not null,"
             "    comment_len   int default 0,"
             "    autoInc       char(1) not null default '0',"
             "    defaultValue  text,"
             "    isNotNull     char(1) not null default '0',"
             "    colLength     int,"
             "    precision     int,"
             "    scale         int,"
             "    constraint tabColumn_fk"
             "       foreign key(tabid) references tabTable(id))",
             "create table tabIndex"
             "   (id            varchar(50) not null primary key,"
             "    tabid         varchar(50) not null,"
             "    name          varchar(64) not null,"
             "    isPrimary     char(1) not null default '0',"
             "    isUnique      char(1) not null default '0',"
             "    constraint tabIndex_fk"
             "       foreign key(tabid) references tabTable(id))",
             "create table tabIndexCol"
             "   (id                varchar(50) not null primary key,"
             "    idxid             varchar(50) not null,"
             "    colid             varchar(50) not null,"
             "    seq               int not null,"
             "    constraint tabIndexCol_fk1"
             "       foreign key(idxid) references tabIndex(id),"
             "    constraint tabIndexCol_fk2"
             "       foreign key(colid) references tabColumn(id))",
             "create table tabForeignKey"
             "   (id                  varchar(50) not null primary key,"
             "    tabid               varchar(50) not null,"
             "    name                varchar(64),"
             "    reftabid            varchar(50) not null,"
             "    constraint tabForeignKey_u"
             "       unique(name))",
             "create table tabFKCol"
             "   (fkid              varchar(50) not null,"
             "    colid             varchar(50) not null,"
             "    refcolid          varchar(50) not null,"
             "    seq               int not null,"
             "    constraint tabFKcol_pk"
             "       primary key(fkid,colid),"
             "    constraint tabFKCol_fk1"
             "       foreign key(fkid) references tabForeignKey(id),"
             "    constraint tabFKCol_fk2"
             "       foreign key(colid) references tabColumn(id),"
             "    constraint tabFKCol_fk3"
             "       foreign key(refcolid) references tabColumn(id))",
             NULL};

extern int insert_table(TABTABLE_T *t) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           ret = 0;

    if (t && t->id[0]) {
      // Must check that REPLACE doesn't mess up with FKs
      _must_succeed("insert table",
                    sqlite3_prepare_v2(G_db,
                                "insert or replace into tabTable(id, name,"
                                "comment_len)"
                                " values(?1,lower(?2),?3)",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
      if ((sqlite3_bind_text(stmt, 1,
                            (const char*)t->id, -1,
                            SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 2,
                                (const char*)t->name, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 3, (int)t->comment_len) != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        fprintf(stderr, "Failure inserting/updating table\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        ret = -1;
      }
      sqlite3_finalize(stmt);
      memset(t, 0, sizeof(TABTABLE_T));
    }
    return ret;
}

extern int insert_column(TABCOLUMN_T *c) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    char          ok = 1;

    if (c && c->id[0]) {
      _must_succeed("insert column",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabColumn(id, tabid,"
                                " name,dataType,autoInc,defaultValue,"
                                " isNotNull,colLength,precision,scale,"
                                " comment_len)"
                                " values(?1,?2,lower(?3),lower(?4),?5,?6,"
                                "?7,?8,?9,?10,?11)",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
      if ((sqlite3_bind_text(stmt, 1,
                            (const char*)c->id, -1,
                            SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 2,
                                (const char*)c->tabid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 3,
                                (const char*)c->name, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 4,
                                (const char*)c->datatype, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 5,
                                (const char*)&(c->autoinc), 1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 7,
                                (const char*)&(c->isnotnull), 1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 8, (int)c->collength) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 9, (int)c->precision) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 10, (int)c->scale) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 11, (int)c->comment_len) != SQLITE_OK)) {
        fprintf(stderr, "Binding error\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        ok = 0;
      }
      if (c->defaultvalue) {
        if (ok && (sqlite3_bind_text(stmt, 6, (const char *)c->defaultvalue,
                                     -1, SQLITE_STATIC) != SQLITE_OK)) {
          fprintf(stderr, "Binding error\n");
          fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
          ok = 0;
        }
      } else {
        if (ok && (sqlite3_bind_null(stmt, 6) != SQLITE_OK)) {
          fprintf(stderr, "Binding error\n");
          fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
          ok = 0;
        }
      }
      if (ok && (sqlite3_step(stmt) != SQLITE_DONE)) {
        fprintf(stderr, "Failure inserting column\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
      }
      sqlite3_finalize(stmt);
      if (c->defaultvalue) {
        free(c->defaultvalue);
      }
      memset(c, 0, sizeof(TABCOLUMN_T));
    }
    return (ok ? 0 : -1);
}

extern int insert_index(TABINDEX_T *i) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           ret = 0;

    if (i && i->id[0]) {
      _must_succeed("insert index",
                    sqlite3_prepare_v2(G_db,
                                "insert or replace into tabIndex(id, tabid,"
                                "name,isPrimary,isUnique)"
                                " values(?1,?2,lower(?3),?4,?5)",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
      if ((sqlite3_bind_text(stmt, 1,
                            (const char*)i->id, -1,
                            SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 2,
                                (const char*)i->tabid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 3,
                                (const char*)i->name, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 4,
                                (const char*)&(i->isprimary), 1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 5,
                                (const char*)&(i->isunique), 1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        fprintf(stderr, "Failure inserting/updating index\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        ret = -1;
      }
      sqlite3_finalize(stmt);
      memset(i, 0, sizeof(TABINDEX_T));
    }
    return ret;
}

extern int insert_indexcol(TABINDEXCOL_T *ic) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           ret = 0;

    if (ic && ic->id[0]) {
      _must_succeed("insert index",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabIndexCol(id,idxid,colid,seq)"
                                " values(?1,?2,?3,?4)",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
      if ((sqlite3_bind_text(stmt, 1,
                            (const char*)ic->id, -1,
                            SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 2,
                                (const char*)ic->idxid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 3,
                                (const char*)ic->colid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 4, (int)ic->seq) != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        fprintf(stderr, "Failure inserting index column\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        ret = -1;
      }
      sqlite3_finalize(stmt);
      memset(ic, 0, sizeof(TABINDEXCOL_T));
    }
    return 0;
}

extern int insert_foreignkey(TABFOREIGNKEY_T *fk, COLFOREIGNKEY_T *cols) {
    COLFOREIGNKEY_T *c;
    int              i;
    sqlite3_stmt    *stmt;
    char            *ztail = NULL;
    sqlite3_stmt    *stmt2;
    char            *ztail2 = NULL;
    char             ok = 1;

    if (fk && fk->id[0]) {
      _must_succeed("insert foreign key",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabForeignKey(id,tabid,"
                                "name,reftabid)"
                                " values(?1,?2,lower(?3),?4)",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
      _must_succeed("insert foreign key column",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabFKcol(fkid,colid,"
                                "refcolid,seq)"
                                " values(?1,?2,?3,?4)",
                                -1, 
                                &stmt2,
                                (const char **)&ztail2));
      if ((sqlite3_bind_text(stmt, 1,
                            (const char*)fk->id, -1,
                            SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 2,
                                (const char*)fk->tabid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 3,
                                (const char*)fk->name, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 4,
                                (const char*)fk->reftabid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        fprintf(stderr, "Failure inserting foreign key\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        ok = 0;
      }
      c = cols;
      if (ok && c) {
        i = 1;
        while (ok && c) {
          if ((sqlite3_bind_text(stmt2, 1,
                                (const char*)fk->id, -1,
                                SQLITE_STATIC) != SQLITE_OK)
              || (sqlite3_bind_text(stmt2, 2,
                                    (const char*)c->colid, -1,
                                    SQLITE_STATIC) != SQLITE_OK)
              || (sqlite3_bind_text(stmt2, 3,
                                    (const char*)c->refcolid, -1,
                                    SQLITE_STATIC) != SQLITE_OK)
              || (sqlite3_bind_int(stmt2, 4, i) != SQLITE_OK)
              || (sqlite3_step(stmt2) != SQLITE_DONE)) {
            fprintf(stderr, "Failure inserting foreign key column\n");
            fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
            ok = 0;
          }
          (void)sqlite3_reset(stmt2);
          (void)sqlite3_clear_bindings(stmt2);
          c = c->next;
          i++;
        }
      }
      sqlite3_finalize(stmt);
      sqlite3_finalize(stmt2);
      memset(fk, 0, sizeof(TABFOREIGNKEY_T));
    }
    return (ok ? 0 : -1);
}

extern int db_begin_tx(void) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;

    _must_succeed("begin tx",
                  sqlite3_prepare_v2(G_db,
                            "begin transaction",
                            -1, 
                            &stmt,
                            (const char **)&ztail));
    if (sqlite3_step(stmt) != SQLITE_DONE) {
       fprintf(stderr, "Failure beginning transaction");
    }
    sqlite3_finalize(stmt);
    return 0;
}

extern int db_commit(void) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;

    _must_succeed("commit",
                  sqlite3_prepare_v2(G_db,
                            "commit transaction",
                            -1, 
                            &stmt,
                            (const char **)&ztail));
    if (sqlite3_step(stmt) != SQLITE_DONE) {
       fprintf(stderr, "Failure committing transaction");
    }
    sqlite3_finalize(stmt);
    return 0;
}

extern int db_rollback(void) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;

    _must_succeed("rollback",
                  sqlite3_prepare_v2(G_db,
                            "rollback transaction",
                            -1, 
                            &stmt,
                            (const char **)&ztail));
    if (sqlite3_step(stmt) != SQLITE_DONE) {
       fprintf(stderr, "Failure rolling back transaction");
    }
    sqlite3_finalize(stmt);
    return 0;
}

static void db_init(void) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           i = 0;

    while (G_ddl[i]) {
      if ((sqlite3_prepare_v2(G_db,
                              G_ddl[i], 
                              -1, 
                              &stmt,
                              (const char **)&ztail)  != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        fprintf(stderr, "Failure executing:\n%s\n", G_ddl[i]);
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        exit(1);
      }
      sqlite3_finalize(stmt);
      i++;
    }
}

extern int db_connect(void) {
    // Returns 0 if OK, -1 if failure
    int           rc = -1;
    int           ret;

    if (debugging()) {
      ret = sqlite3_open("mwbgrader.sqlite", &G_db);
    } else {
      ret = sqlite3_open(":memory:", &G_db);
    }
    if (ret == SQLITE_OK) {
      db_init();
      rc = 0;
    } else {
      fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
    }
    return rc;
}

extern void db_disconnect(void) {
    if (G_db) {
      sqlite3_close(G_db);
      G_db = NULL;
    }
}

extern int db_runcheck(short check_code, char report, char *query, char *arg) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           rowcnt = -1;
    int           rc;
    char          pct = 0;
    int           pct_val = -1;
    int           to_bind;

    if (G_db && query) {
      if (debugging()) {
        printf("Running check %s\n", grad_keyword(check_code));
      }
      if (strncmp(grad_keyword(check_code), "percent_", 8) == 0) {
        pct = 1;
      }
      _must_succeed("run check",
                    sqlite3_prepare_v2(G_db,
                                       query,
                                       -1, 
                                       &stmt,
                                       (const char **)&ztail));
      // Depending on the query there may be to
      // bind "arg"
      to_bind = sqlite3_bind_parameter_count(stmt);
      if (to_bind) {
        if (sqlite3_bind_text(stmt, 1,
                              (const char*)arg, -1,
                              SQLITE_STATIC) != SQLITE_OK) {
          fprintf(stderr, "Failure binding:\n%s\n", arg);
          fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
          // Let it fail ...
        }
      }
      rowcnt = 0;
      while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (pct) {
          pct_val = sqlite3_column_int(stmt, 0);
        }
        if (report) {
          // The query is expected to only return a
          // list of names. It could be checked dynamically
          if (pct) {
            printf("\t%d%%\n", pct_val);
          } else {
            printf("\t%s\n", sqlite3_column_text(stmt, 0));
          }
        }
        rowcnt++;
      }
      if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failure executing:\n%s\n", query);
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        rowcnt = -1;
      } else {
        if (report && !rowcnt) {
          printf("\t*** No problem found\n");
        }
      }
      sqlite3_finalize(stmt);
    }
    return (pct ? pct_val : rowcnt);
}

extern RELATIONSHIP_T *db_relationships(void) {
    sqlite3_stmt   *stmt;
    char           *ztail = NULL;
    int             rc;
    RELATIONSHIP_T *list = NULL;
    RELATIONSHIP_T *r = NULL;

    if (G_db) {
      rc = sqlite3_prepare_v2(G_db,
                             "select t1.name as tablename1,"
                             "  cardinality,"
                             "  t2.name as tablename2"
                             " from (select case dummy.n"
                             "  when 1 then tabid1"
                             "  else tabid2"
                             "  end as tabid1,"
                             "  'many to many' as cardinality,"
                             "  case dummy.n"
                             "  when 1 then tabid2"
                             "  else tabid1"
                             "  end as tabid2"
                             " from (select ut.tabid,"
                             "   min(fk.reftabid) tabid1,"
                             "   max(fk.reftabid) tabid2"
                             " from (select t.id as tabid"
                             "   from tabTable t"
                             "   left outer join tabForeignKey fk"
                             "   on fk.reftabid=t.id"
                             "  where fk.id is null) ut" // Unreferenced tables
                             "  join tabForeignKey fk"
                             "  on fk.tabid=ut.tabid"
                             "  group by ut.tabid"
                             "  having count(*)=2) x"
                             " cross join (select 1 as n"
                             "   union all"
                             "   select 2 as n) dummy"
                             " union all"
                             " select f.reftabid as tabid1,"
                             "  case sum(c.isnotnull)"
                             "  when 0 then '0 to many'"
                             "  else '1 to many'"
                             "  end as cardinality,"
                             "  f.tabid as tabid2"
                             " from (select fk.id as fkid,fk.tabid,"
                             "fk.reftabid,fk.name"
                             " from tabForeignKey fk"
                             " left outer join (select ut.tabid"
                             "    from (select t.id as tabid"
                             "      from tabTable t"
                             "      left outer join tabForeignKey fk"
                             "      on fk.reftabid=t.id"
                             "    where fk.id is null) ut"
                                                    // Unreferenced tables
                             "   join tabForeignKey fk"
                             "   on fk.tabid=ut.tabid"
                             " group by ut.tabid"
                             " having count(*)=2) m_n_tab"
                                          // Tables implementing a many-to-many
                                          // relationship
                             " on m_n_tab.tabid=fk.tabid"
                             " where m_n_tab.tabid is null) f"
                             " join tabFKcol fc"
                             " on fc.fkid=f.fkid"
                             " join tabcolumn c"
                             " on fc.colid=c.id"
                             " group by f.tabid,f.reftabid,f.name) z"
                             " join tabTable t1"
                             " on t1.id=z.tabid1"
                             " join tabTable t2"
                             " on t2.id=z.tabid2"
                             " order by t1.name",
                                       -1, 
                                       &stmt,
                                       (const char **)&ztail);
      if (rc != SQLITE_OK) {
        fprintf(stderr, "Failure parsing relationship query\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        return NULL;
      }
      while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        r = new_relationship((char *)sqlite3_column_text(stmt, 0),
                             (char *)sqlite3_column_text(stmt, 1),
                             (char *)sqlite3_column_text(stmt, 2));
        add_relationship(&list, r);
      }
      if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failure executing relationship query\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
      }
      sqlite3_finalize(stmt);
    }
    return list;
}

extern NAME_ITEM_T *db_selfref(void) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           rc;
    NAME_ITEM_T  *list = NULL;
    NAME_ITEM_T  *n = NULL;

    if (G_db) {
      rc = sqlite3_prepare_v2(G_db,
                              "select t.name as TableName"
                              " from (select distinct tabid"
                              "  from tabForeignKey"
                              "  where tabid=reftabid) x"
                              " join tabTable t"
                              " on t.id=x.tabid",
                                       -1, 
                                       &stmt,
                                       (const char **)&ztail);
      if (rc != SQLITE_OK) {
        fprintf(stderr, "Failure parsing relationship query\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        return NULL;
      }
      while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        n = new_name((char *)sqlite3_column_text(stmt, 0));
        add_name(&list, n);
      }
      if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failure executing self reference query\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
      }
      sqlite3_finalize(stmt);
    }
    return list;
}

extern int db_basic_info(int *ptabcnt, int *pdatacnt) {
    // Number of managed pieces of data, that is including whatever
    // is system-generated and not counting columns involved in FK
    // relationships several times
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           rc;
    int           ret = 0;

    if (G_db && ptabcnt && pdatacnt) {
      rc = sqlite3_prepare_v2(G_db,
                              "select count(distinct c.tabid) table_count,"
                              " count(*) as pieces_of_data"
                              " from tabColumn c"
                              " left outer join tabForeignKey fk"
                              " on fk.tabid=c.tabid"
                              " left outer join tabFKcol as fkc"
                              " on fkc.fkid=fk.id"
                              " and fkc.colid=c.id"
                              " where c.autoInc=0"
                              " and fkc.colid is null",
                                       -1, 
                                       &stmt,
                                       (const char **)&ztail);
      if (rc != SQLITE_OK) {
        fprintf(stderr, "Failure parsing basic info query\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        return -1;
      }
      if ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        *ptabcnt = sqlite3_column_int(stmt, 0);
        *pdatacnt = sqlite3_column_int(stmt, 1);
      } else {
        fprintf(stderr, "Failure executing basic info query\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        ret = -1;
      }
      sqlite3_finalize(stmt);
    }
    return ret;
}
