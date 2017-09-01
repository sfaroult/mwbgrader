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
static char *G_refddl[] =
            {"create table tabVariant"
             "   (id          integer primary key,"
             "    model_name  text not null,"
             "    max_points  int not null default 100,"
             "    constraint tabVariant_uq unique(model_name))",
             "create table tabTable"
             "   (id          integer not null primary key,"
             "    varid       int not null,"
             "    mwb_id      varchar(50) not null unique,"
             "    name        varchar(64) not null,"
             "    comment_len int default 0,"
             "    constraint tabTable_fk foreign key(varid)"
             "               references tabVariant(id),"
             "    constraint tabTable_u"
             "       unique (varid,name))",
             NULL};

static char *G_ddl[] =
            {"create table tabTable"
             "   (id          integer not null primary key,"
             "    varid       int not null default 0,"
             "    mwb_id      varchar(50) not null unique,"
             "    name        varchar(64) not null,"
             "    comment_len int default 0,"
             "    constraint tabTable_u1"
             "       unique (name, varid),"
             "    constraint tabTable_u2"
             "       unique (mwb_id, varid))",
             "create table tabColumn"
             "   (id            integer primary key,"
             "    mwb_id        varchar(50) not null,"
             "    tabid         int not null,"
             "    name          varchar(64) not null,"
             "    dataType      varchar(20) not null,"
             "    comment_len   int default 0,"
             "    autoInc       char(1) not null default '0',"
             "    defaultValue  text,"
             "    isNotNull     char(1) not null default '0',"
             "    colLength     int,"
             "    precision     int,"
             "    scale         int,"
             "    constraint tabColumn_u1"
             "       unique (tabid, mwb_id),"
             "    constraint tabColumn_u2"
             "       unique (tabid, name),"
             "    constraint tabColumn_fk"
             "       foreign key(tabid) references tabTable(id))",
             "create table tabIndex"
             "   (id            integer primary key,"
             "    mwb_id        varchar(50) not null,"
             "    tabid         int not null,"
             "    name          varchar(64) not null,"
             "    isPrimary     char(1) not null default '0',"
             "    isUnique      char(1) not null default '0',"
             "    constraint tabIndex_u1"
             "       unique(mwb_id, tabid),"
             "    constraint tabIndex_u2"
             "       unique(name, tabid),"
             "    constraint tabIndex_fk"
             "       foreign key(tabid) references tabTable(id))",
             "create table tabIndexCol"
             "   (idxid         int not null,"
             "    colid         int not null,"
             "    seq           int not null,"
             "    constraint tabIndexCol_pk"
             "       primary key(idxid, colid),"
             "    constraint tabIndexCol_fk1"
             "       foreign key(idxid) references tabIndex(id),"
             "    constraint tabIndexCol_fk2"
             "       foreign key(colid) references tabColumn(id))",
             "create table tabForeignKey"
             "   (id            integer primary key,"
             "    mwb_id        varchar(50) not null,"
             "    tabid         int not null,"
             "    name          varchar(64),"
             "    reftabid      int not null,"
             "    constraint tabForeignKey_u1"
             "       unique(name,tabid),"
             "    constraint tabForeignKey_u2"
             "       unique(mwb_id,tabid),"
             "    constraint tabForeignKey_fk1"
             "       foreign key(tabid) references tabTable(id),"
             "    constraint tabForeignKey_fk2"
             "       foreign key(reftabid) references tabTable(id))",
             "create table tabFKCol"
             "   (fkid          int not null,"
             "    colid         int not null,"
             "    refcolid      int not null,"
             "    seq           int not null,"
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
    int           ret = -1;

    if (t) {
      ret = 0;
      if (debugging()) {
        fprintf(stderr, ">> insert_table(%s[%s])\n", t->id, t->name);
      }
      _must_succeed("insert table",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabTable(mwb_id,"
                                "name,comment_len,varid)"
                                " values(?1,lower(?2),?3,?4)",
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
          || (sqlite3_bind_int(stmt, 4, (int)t->varid) != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        char fail = 1;
        if ((sqlite3_errcode(G_db) == SQLITE_CONSTRAINT)
            && (sqlite3_extended_errcode(G_db) == SQLITE_CONSTRAINT_UNIQUE)
            && (strcmp(t->name, t->id) != 0)) {
          fail = 0; 
          sqlite3_finalize(stmt);
          _must_succeed("update table",
                    sqlite3_prepare_v2(G_db,
                                "update tabTable"
                                " set name=lower(?1),"
                                "     comment_len=?2"
                                " where varid=?3"
                                " and mwb_id=?4",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
          if ((sqlite3_bind_text(stmt, 1,
                                (const char*)t->name, -1,
                                SQLITE_STATIC) != SQLITE_OK)
               || (sqlite3_bind_int(stmt, 2, (int)t->comment_len) != SQLITE_OK)
               || (sqlite3_bind_int(stmt, 3, (int)t->varid) != SQLITE_OK)
               || (sqlite3_bind_text(stmt, 4,
                            (const char*)t->id, -1,
                            SQLITE_STATIC) != SQLITE_OK)
               || (sqlite3_step(stmt) != SQLITE_DONE)) {
            fail = 1;
          }
        }
        if (fail) {
          if (strcmp(t->name, t->id)) {
            fprintf(stderr, "Failure inserting/updating table\n");
            fprintf(stderr, "%s (err code: %d, extended: %d\n",
                            sqlite3_errmsg(G_db),
                            sqlite3_errcode(G_db),
                            sqlite3_extended_errcode(G_db));
          } else {
            if (debugging()) {
               fprintf(stderr, " %s [%s] already known\n", t->id, t->name);
            }
          }
          ret = -1;
        }
      }
      sqlite3_finalize(stmt);
    }
    if (debugging()) {
      fprintf(stderr, "<< insert_table - %s\n", (ret == 0 ? "SUCCESS":"FAILURE"));
    }
    return ret;
}

extern int insert_column(TABCOLUMN_T *c) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    char          ok = 1;

    if (c) {
      if (debugging()) {
        fprintf(stderr, ">> insert_column(%s[%s]) table %s\n",
                        c->id, c->name, c->tabid);
      }
      _must_succeed("insert column",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabColumn(mwb_id,tabid,"
                                " name,dataType,autoInc,defaultValue,"
                                " isNotNull,colLength,precision,scale,"
                                " comment_len)"
                                " select ?1,id,lower(?3),lower(?4),?5,?6,"
                                "?7,?8,?9,?10,?11"
                                " from tabTable where mwb_id=?2"
                                " and varid=?12",
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
          || (sqlite3_bind_int(stmt, 11, (int)c->comment_len) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 12, (int)c->varid) != SQLITE_OK)) {
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
        char fail = 1;
        if ((sqlite3_errcode(G_db) == SQLITE_CONSTRAINT)
            && (sqlite3_extended_errcode(G_db) == SQLITE_CONSTRAINT_UNIQUE)
            && (strcmp(c->name, c->id) != 0)) {
          fail = 0; 
          sqlite3_finalize(stmt);
          _must_succeed("update column",
                    sqlite3_prepare_v2(G_db,
                                "update tabColumn"
                                " set name=lower(?1),"
                                "     datatype=lower(?2),"
                                "     autoInc=?3,"
                                "     defaultValue=?4,"
                                "     isNotNull=?5,"
                                "     collength=?6,"
                                "     precision=?7,"
                                "     scale=?8,"
                                "     comment_len=?9"
                                " where tabid=(select id from tabTable"
                                "     where mwb_id=?10"
                                "       and varid=?11)"
                                " and mwb_id=?12",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
          if ((sqlite3_bind_text(stmt, 12,
                                 (const char*)c->id, -1,
                                 SQLITE_STATIC) != SQLITE_OK)
              || (sqlite3_bind_text(stmt, 10,
                                    (const char*)c->tabid, -1,
                                    SQLITE_STATIC) != SQLITE_OK)
              || (sqlite3_bind_text(stmt, 1,
                                    (const char*)c->name, -1,
                                    SQLITE_STATIC) != SQLITE_OK)
              || (sqlite3_bind_text(stmt, 2,
                                    (const char*)c->datatype, -1,
                                    SQLITE_STATIC) != SQLITE_OK)
              || (sqlite3_bind_text(stmt, 3,
                                    (const char*)&(c->autoinc), 1,
                                    SQLITE_STATIC) != SQLITE_OK)
              || (sqlite3_bind_text(stmt, 5,
                                    (const char*)&(c->isnotnull), 1,
                                    SQLITE_STATIC) != SQLITE_OK)
              || (sqlite3_bind_int(stmt, 6, (int)c->collength) != SQLITE_OK)
              || (sqlite3_bind_int(stmt, 7, (int)c->precision) != SQLITE_OK)
              || (sqlite3_bind_int(stmt, 8, (int)c->scale) != SQLITE_OK)
              || (sqlite3_bind_int(stmt, 9, (int)c->comment_len) != SQLITE_OK)
              || (sqlite3_bind_int(stmt, 11, (int)c->varid) != SQLITE_OK)) {
            fprintf(stderr, "Binding error\n");
            fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
            ok = 0;
          }
          if (c->defaultvalue) {
            if (ok && (sqlite3_bind_text(stmt, 4, (const char *)c->defaultvalue,
                                         -1, SQLITE_STATIC) != SQLITE_OK)) {
              fprintf(stderr, "Binding error\n");
              fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
              ok = 0;
            }
          } else {
            if (ok && (sqlite3_bind_null(stmt, 4) != SQLITE_OK)) {
              fprintf(stderr, "Binding error\n");
              fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
              ok = 0;
            }
          }
          if (ok && (sqlite3_step(stmt) != SQLITE_DONE)) {
            fail = 1;
          }
        }
        if (fail) {
          if (strcmp(c->name, c->id)) {
            fprintf(stderr, "Failure inserting/updating column\n");
            fprintf(stderr, "%s (err code: %d, extended: %d\n",
                            sqlite3_errmsg(G_db),
                            sqlite3_errcode(G_db),
                            sqlite3_extended_errcode(G_db));
          } else {
            if (debugging()) {
               fprintf(stderr, "OK - %s [%s] already known\n", c->id, c->name);
            }
          }
          ok = 0;
        }
      }
      if (ok && (sqlite3_changes(G_db) == 0)) {
        // Table probably not inserted yet
        TABTABLE_T t;
        memset(&t, 0, sizeof(TABTABLE_T));
        t.varid = c->varid;
        strncpy(t.id, c->tabid, ID_LEN);
        strncpy(t.name, c->tabid, NAME_LEN);
        t.comment_len = 0;
        if (insert_table(&t) == 0) {
          ok = (insert_column(c) != -1);
        }
      }
      sqlite3_finalize(stmt);
      if (c->defaultvalue) {
        free(c->defaultvalue);
      }
      memset(c, 0, sizeof(TABCOLUMN_T));
    }
    if (debugging()) {
      fprintf(stderr, "<< insert_column - %s\n", (ok ? "SUCCESS":"FAILURE"));
    }
    return (ok ? 0 : -1);
}

extern int insert_index(TABINDEX_T *i) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           ret = -1;

    if (i) {
      ret = 0;
      if (debugging()) {
        fprintf(stderr, ">> insert_index(%s[%s]) table %s\n",
                        i->id, i->name, i->tabid);
      }
      _must_succeed("insert index",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabIndex(mwb_id,tabid,"
                                "name,isPrimary,isUnique)"
                                " select ?1,id,lower(?3),?4,?5"
                                " from tabTable where mwb_id=?2"
                                " and varid=?6",
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
          || (sqlite3_bind_int(stmt, 6, (int)i->varid) != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        char fail = 1;
        if ((sqlite3_errcode(G_db) == SQLITE_CONSTRAINT)
            && (sqlite3_extended_errcode(G_db) == SQLITE_CONSTRAINT_UNIQUE)) {
          fail = 0; 
          sqlite3_finalize(stmt);
          _must_succeed("update index",
                    sqlite3_prepare_v2(G_db,
                                "update tabIndex"
                                " set name=lower(?1),"
                                "     isPrimary=?2,"
                                "     isUnique=?3"
                                " where tabid=(select id from tabTable"
                                "     where varid=?4"
                                "       and mwb_id=?5)"
                                " and mwb_id=?6",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
          if ((sqlite3_bind_text(stmt, 1,
                                (const char*)i->name, -1,
                                SQLITE_STATIC) != SQLITE_OK)
               || (sqlite3_bind_text(stmt, 2,
                                (const char*)&(i->isprimary), 1,
                                SQLITE_STATIC) != SQLITE_OK)
               || (sqlite3_bind_text(stmt, 3,
                                (const char*)&(i->isunique), 1,
                                SQLITE_STATIC) != SQLITE_OK)
               || (sqlite3_bind_int(stmt, 4, (int)i->varid) != SQLITE_OK)
               || (sqlite3_bind_text(stmt, 5,
                                (const char*)i->tabid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
               || (sqlite3_bind_text(stmt, 6,
                                (const char*)i->id, -1,
                                SQLITE_STATIC) != SQLITE_OK)
               || (sqlite3_step(stmt) != SQLITE_DONE)) {
            fail = 1;
          }
        }
        if (fail) {
          fprintf(stderr, "Failure inserting/updating index\n");
          fprintf(stderr, "%s (err code: %d, extended: %d\n",
                        sqlite3_errmsg(G_db),
                        sqlite3_errcode(G_db),
                        sqlite3_extended_errcode(G_db));
          ret = -1;
        }
      }
      sqlite3_finalize(stmt);
    }
    if (debugging()) {
      fprintf(stderr, "<< insert_index - %s\n", (ret == 0 ? "SUCCESS":"FAILURE"));
    }
    return ret;
}

extern int insert_indexcol(TABINDEXCOL_T *ic) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           ret = -1;

    if (ic) {
      ret = 0;
      if (debugging()) {
        fprintf(stderr, ">> insert_indexcol(index %s col %s)\n",
                        ic->idxid, ic->colid);
      }
      _must_succeed("insert indexcol",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabIndexCol(idxid,colid,seq)"
                                " select i.id,c.id,?3"
                                " from tabIndex i"
                                " join tabColumn c"
                                "  on c.tabid=i.tabid"
                                " join tabTable t"
                                "  on t.id=i.tabid"
                                " where i.mwb_id=?1"
                                " and c.mwb_id=?2"
                                " and t.varid=?4",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
      if ((sqlite3_bind_text(stmt, 1,
                                (const char*)ic->idxid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 2,
                                (const char*)ic->colid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 3, (int)ic->seq) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 4, (int)ic->varid) != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        fprintf(stderr, "Failure inserting index column\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        ret = -1;
      }
      if (!ret && (sqlite3_changes(G_db) == 0)) {
        // Index and table are necessarily inserted,
        // the column may be missing
        TABCOLUMN_T c;
        char        insert_col;

        if (debugging()) {
          fprintf(stderr, "No error but index column not inserted\n");
        }
        memset(&c, 0, sizeof(TABCOLUMN_T));
        c.varid = ic->varid;
        strncpy(c.tabid, ic->tabid, ID_LEN);
        strncpy(c.id, ic->colid, ID_LEN);
        strncpy(c.name, ic->colid, NAME_LEN);
        insert_col = insert_column(&c);
        if (debugging()) {
          fprintf(stderr, "(IDX col) Insertion of %s: %s\n", c.id,
                              (insert_col ? "FAILED" : "SUCCEEDED"));
        }
        if (insert_col == 0) { // Try again
          ret = insert_indexcol(ic);
        } else {
          ret = -1;
        }
      }
      sqlite3_finalize(stmt);
    }
    if (debugging()) {
      fprintf(stderr, "<< insert_indexcol - %s\n", (ret == 0 ? "SUCCESS":"FAILURE"));
    }
    return ret;
}

static int insert_fkcol(TABFOREIGNKEY_T *fk, COLFOREIGNKEY_T *fkc, int i) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           ret = -1;

    if (fk && fkc) {
      ret = 0;
      if (debugging()) {
        fprintf(stderr, ">> insert_fkcol(fk %s, col %s, rcol %s)\n",
                        fk->id, fkc->colid, fkc->refcolid);
      }
      _must_succeed("insert fkcol",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabFKcol(fkid,colid,"
                                "refcolid,seq)"
                                " select fk.id,c1.id,c2.id,?4"
                                " from tabForeignKey fk"
                                "  join tabColumn c1"
                                "  on c1.tabid=fk.tabid"
                                "  join tabTable t1"
                                "  on t1.id=c1.tabid"
                                "  join tabColumn c2"
                                "  on c2.tabid=fk.reftabid"
                                "  join tabTable t2"
                                "  on t2.id=c2.tabid"
                                " where fk.mwb_id=?1"
                                " and c1.mwb_id=?2"
                                " and c2.mwb_id=?3"
                                " and t1.varid=t2.varid"
                                " and t1.varid=?5",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
      if ((sqlite3_bind_text(stmt, 1,
                             (const char*)fk->id, -1,
                             SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 2,
                                (const char*)fkc->colid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_text(stmt, 3,
                                (const char*)fkc->refcolid, -1,
                                SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 4, i) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 5, (int)fk->varid) != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        fprintf(stderr, "Failure inserting foreign key column\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        ret = -1;
      }
      if (!ret && (sqlite3_changes(G_db) == 0)) {
        // At this stage foreign key and tables
        // are necessarily inserted. Only columns
        // may be missing.
        TABCOLUMN_T c;
        char        insert_col = 1;

        if (debugging()) {
          fprintf(stderr, "No error but foreign key column not inserted\n");
        }
        memset(&c, 0, sizeof(TABCOLUMN_T));
        c.varid = fk->varid;
        strncpy(c.tabid, fk->tabid, ID_LEN);
        strncpy(c.id, fkc->colid, ID_LEN);
        strncpy(c.name, fkc->colid, NAME_LEN);
        insert_col &= insert_column(&c);
        if (debugging()) {
          fprintf(stderr, "(FK col) Insertion of %s: %s\n", fkc->colid,
                              (insert_col ? "FAILED" : "SUCCEEDED"));
        }
        // The other one
        strncpy(c.tabid, fk->reftabid, ID_LEN);
        strncpy(c.id, fkc->refcolid, ID_LEN);
        strncpy(c.name, fkc->refcolid, NAME_LEN);
        insert_col &= insert_column(&c);
        if (debugging()) {
          fprintf(stderr, "(FK col) Insertion of %s: %s\n", fkc->refcolid,
                              (insert_col ? "FAILED" : "SUCCEEDED"));
        }
        // Try again
        if (insert_col == 0) {
          ret = insert_fkcol(fk, fkc, i);
        } else {
          ret = -1;
        }
      }
      sqlite3_finalize(stmt);
    }
    if (debugging()) {
      fprintf(stderr, "<< insert_fkcol - %s\n", (ret == 0 ? "SUCCESS":"FAILURE"));
    }
    return ret;
}

extern int insert_foreignkey(TABFOREIGNKEY_T *fk, COLFOREIGNKEY_T *cols) {
    COLFOREIGNKEY_T *c;
    int              i;
    sqlite3_stmt    *stmt;
    char            *ztail = NULL;
    char             ok = 0;
    char             cols_done = 0;

    if (fk) {
      ok = 1;
      if (debugging()) {
        fprintf(stderr, "insert_foreignkey(%s [%s]) tables %s->%s\n",
                        fk->id, fk->name, fk->tabid, fk->reftabid);
      }
      _must_succeed("insert foreign key",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabForeignKey(mwb_id,tabid,"
                                "name,reftabid)"
                                " select ?1,t1.id,lower(?3),t2.id"
                                " from tabTable t1"
                                "   join tabTable t2"
                                "   on t2.varid=t1.varid"
                                " where t1.mwb_id=?2"
                                " and t2.mwb_id=?4"
                                " and t1.varid=?5",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
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
          || (sqlite3_bind_int(stmt, 5, (int)fk->varid) != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        if ((sqlite3_errcode(G_db) != SQLITE_CONSTRAINT)
            || (sqlite3_extended_errcode(G_db) != SQLITE_CONSTRAINT_UNIQUE)) {
          fprintf(stderr, "Failure inserting foreign key\n");
          fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
          ok = 0;
        }
      } else {
        if (debugging()) {
          fprintf(stderr, "-- Insertion of foreign key %s worked\n", fk->name);
        }
      }
      if (ok && (sqlite3_changes(G_db) == 0)) {
        // Table probably not inserted yet
        TABTABLE_T t;
        char       insert_tab = 1;

        if (debugging()) {
          fprintf(stderr, "No error but foreign key %s not inserted\n", fk->name);
        }
        memset(&t, 0, sizeof(TABTABLE_T));
        t.varid = fk->varid;
        strncpy(t.id, fk->tabid, ID_LEN);
        strncpy(t.name, fk->tabid, NAME_LEN);
        t.comment_len = 0;
        insert_tab &= insert_table(&t);
        if (debugging()) {
          fprintf(stderr, "(FK) Insertion of %s: %s\n", t.id,
                              (insert_tab ? "FAILED" : "SUCCEEDED"));
        }
        // The other one
        strncpy(t.id, fk->reftabid, ID_LEN);
        strncpy(t.name, fk->reftabid, NAME_LEN);
        insert_tab &= insert_table(&t);
        if (debugging()) {
          fprintf(stderr, "(FK) Insertion of %s: %s\n", t.id,
                              (insert_tab ? "FAILED" : "SUCCEEDED"));
        }
        // Try again
        if (insert_tab == 0) {
          ok = (insert_foreignkey(fk, cols) != -1); 
          cols_done = 1;
        } else {
          ok = 0;
        }
      }
      sqlite3_finalize(stmt);
      c = cols;
      if (!cols_done && c) {
        i = 1;
        while (ok && c) {
          ok = (insert_fkcol(fk, c, i) != -1);
          c = c->next;
          i++;
        }
      }
    }
    if (debugging()) {
      fprintf(stderr, "<< insert_foreignkey - %s\n", (ok ? "SUCCESS":"FAILURE"));
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
                             " order by 1,3,2",
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
