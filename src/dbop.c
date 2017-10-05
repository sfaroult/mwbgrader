#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sqlite3.h>
#include <locale.h>
#include <sys/stat.h>

static sqlite3 *G_db = NULL;
static char     G_refdb[FILENAME_MAX] = "";
static char     G_ref = 0; // Just to know which database is the main one

#include "schema.h"
#include "grad.h"
#include "dbop.h"
#include "strbuf.h"
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

// Functions added to SQLite (Levenshtein distance and square root)
extern void sql_lev(sqlite3_context  *context,
                    int               argc,
                    sqlite3_value   **argv);
extern void sql_sqrt(sqlite3_context  *context,
                     int               argc,
                     sqlite3_value   **argv);

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

// Reference table creation
static char *G_refddl[] =
            {"create table tabVariant"
             "   (id          integer primary key,"
             "    model_name  text not null,"
             "    max_points  int not null default 100,"
             "    constraint tabVariant_uq unique(model_name))",
             "create table tabVariantTest"
             "   (varid       int not null,"
             "    testid      int not null,"
             "    pass        int not null default 1,"
             "    constraint tabVariantTest_fk foreign key(varid)"
             "               references tabVariant(id),"
             "    constraint tabVariantTest_pk"
             "               primary key(varid, testid))",
             "create table tabTable"
             "   (id          integer not null primary key,"
             "    varid       int not null,"
             "    mwb_id      varchar(50) not null unique,"
             "    name        varchar(64) not null,"
             "    comment_len int default 0,"
             "    constraint tabTable_fk foreign key(varid)"
             "               references tabVariant(id),"
             "    constraint tabTable_u1"
             "       unique (name, varid),"
             "    constraint tabTable_u2"
             "       unique (mwb_id, varid))",
             NULL};

// In-memory + reference (except first one) table creation
static char *G_ddl[] =
            {"create table tabTable"
             "   (id          integer not null primary key,"
             "    varid       int not null default 0,"
             "    refid       int,"  // Matching table in the reference
             "    mwb_id      varchar(50) not null unique,"
             "    name        varchar(64) not null,"
             "    comment_len int default 0,"
             "    constraint tabTable_u1"
             "       unique (name, varid),"
             "    constraint tabTable_u2"
             "       unique (mwb_id, varid),"
             "    constraint tabTable_u3"
             "       unique (refid))",
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

extern short insert_variant(char *model) {
    // Returns the variant number if successful
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    short         ret = -1;
    char         *p;
    int           maxgrade = 100;
    struct stat   buf;

    if (model) {
      if (debugging()) {
        fprintf(stderr, ">> insert_variant(%s)\n", model);
      }
      if ((p = strchr(model, ':')) != NULL) {
        *p++ = '\0';
        if (sscanf(p, "%d", &maxgrade) != 1) {
          fprintf(stderr, "Invalid grade %s associated with model %s\n",
                          p, model);
          return -1;
        }
        if ((maxgrade <=0) || (maxgrade > 100)) {
          fprintf(stderr, "Invalid grade %d associated with model %s\n",
                          maxgrade, model);
          fprintf(stderr, "(value must be greater than 0, 100 at most)\n");
          return -1;
        }
      }
      // Check that the model actually exists
      if (stat((const char *)model, &buf) == -1) {
        fprintf(stderr, "model %s not found\n", model);
        return -1;
      } 
      // OK, now we can go
      _must_succeed("insert variant",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabVariant(model_name,max_points)"
                                " values(?1,?2)",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
      if ((sqlite3_bind_text(stmt, 1,
                            (const char*)model, -1,
                            SQLITE_STATIC) != SQLITE_OK)
          || (sqlite3_bind_int(stmt, 2, maxgrade) != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        fprintf(stderr, "Failure creating reference variant\n");
        fprintf(stderr, "%s (err code: %d, extended: %d\n",
                        sqlite3_errmsg(G_db),
                        sqlite3_errcode(G_db),
                        sqlite3_extended_errcode(G_db));
      } else {
        ret = (short)sqlite3_last_insert_rowid(G_db);
      }
      sqlite3_finalize(stmt);
    }
    if (debugging()) {
      fprintf(stderr, "<< insert_variant - %hd\n", ret);
    }
    return ret;
}

extern short insert_variant_test(short varid, int testid, char pass) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    short         ret = 0;

    if (debugging()) {
      fprintf(stderr, ">> insert_variant_test(%hd,%d,%hd)\n",
                      varid, testid, (short)pass);
    }
    _must_succeed("insert variant_test",
                    sqlite3_prepare_v2(G_db,
                        "insert into tabVariantTest(varid,testid,pass)"
                        " values(?1,?2,?3)",
                        -1, 
                        &stmt,
                        (const char **)&ztail));
    if ((sqlite3_bind_int(stmt, 1, (int)varid) != SQLITE_OK)
       || (sqlite3_bind_int(stmt, 2, (int)testid) != SQLITE_OK)
       || (sqlite3_bind_int(stmt, 3, (int)pass) != SQLITE_OK)
       || (sqlite3_step(stmt) != SQLITE_DONE)) {
      fprintf(stderr, "Failure inserting pass mark for variant\n");
      fprintf(stderr, "%s (err code: %d, extended: %d\n",
                      sqlite3_errmsg(G_db),
                      sqlite3_errcode(G_db),
                      sqlite3_extended_errcode(G_db));
      ret = -1;
    }
    sqlite3_finalize(stmt);
    if (debugging()) {
      fprintf(stderr, "<< insert_variant_test - %hd\n", ret);
    }
    return ret;
}

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
                                "insert or ignore into tabTable(mwb_id,"
                                "name,comment_len,varid)"
                                " values(ltrim(rtrim(?1,'}'),'{'),lower(?2),?3,?4)",
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
                                " and mwb_id=ltrim(rtrim(?4,'}'),'{')",
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
      fprintf(stderr, "<< insert_table - %s\n",
                      (ret == 0 ? "SUCCESS":"FAILURE"));
    }
    return ret;
}

extern int insert_column(TABCOLUMN_T *c) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    char          ok = 1;
    short         variant;

    if (c) {
      if (debugging()) {
        fprintf(stderr, ">> insert_column(%s[%s]) table %s\n",
                        c->id, c->name, c->tabid);
      }
      variant = c->varid;
      _must_succeed("insert column",
                    sqlite3_prepare_v2(G_db,
                                "insert into tabColumn(mwb_id,tabid,"
                                " name,dataType,autoInc,defaultValue,"
                                " isNotNull,colLength,precision,scale,"
                                " comment_len)"
                                " select ltrim(rtrim(?1,'}'),'{'),id,"
                                "lower(?3),lower(?4),?5,?6,"
                                "?7,?8,?9,?10,?11"
                                " from tabTable"
                                " where mwb_id=ltrim(rtrim(?2,'}'),'{')"
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
                                "     where mwb_id=ltrim(rtrim(?10, '}'),'{')"
                                "       and varid=?11)"
                                " and mwb_id=ltrim(rtrim(?12, '}'),'{')",
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
        } else {
          ok = 0;
        }
      }
      sqlite3_finalize(stmt);
      if (c->defaultvalue) {
        free(c->defaultvalue);
      }
      memset(c, 0, sizeof(TABCOLUMN_T));
      c->varid = variant;
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
                       " select ltrim(rtrim(?1,'}'),'{'),id,lower(?3),?4,?5"
                       " from tabTable where mwb_id=ltrim(rtrim(?2,'}'),'{')"
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
                                "       and mwb_id=ltrim(rtrim(?5,'}'),'{'))"
                                " and mwb_id=ltrim(rtrim(?6,'}'),'{')",
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
      fprintf(stderr, "<< insert_index - %s\n",
                      (ret == 0 ? "SUCCESS":"FAILURE"));
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
                                " where i.mwb_id=ltrim(rtrim(?1,'}'),'{')"
                                " and c.mwb_id=ltrim(rtrim(?2,'}'),'{')"
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
      fprintf(stderr, "<< insert_indexcol - %s\n",
                      (ret == 0 ? "SUCCESS":"FAILURE"));
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
        fflush(stderr);
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
                                " where fk.mwb_id=ltrim(rtrim(?1,'}'),'{')"
                                " and c1.mwb_id=ltrim(rtrim(?2,'}'),'{')"
                                " and c2.mwb_id=ltrim(rtrim(?3,'}'),'{')"
                                " and t1.varid=t2.varid"
                                " and t1.varid=?5",
                                -1, 
                                &stmt,
                                (const char **)&ztail));
      if (debugging()) {
        fprintf(stderr, ">> 1\n"); fflush(stderr);
      }
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
        if (debugging()) {
          fprintf(stderr, ">> 2\n"); fflush(stderr);
        }
        fprintf(stderr, "Failure inserting foreign key column\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        ret = -1;
      }
      if (debugging()) {
        fprintf(stderr, ">> 3\n"); fflush(stderr);
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
      if (debugging()) {
        fprintf(stderr, ">> 4\n");
      }
      sqlite3_finalize(stmt);
    }
    if (debugging()) {
      fprintf(stderr, "<< insert_fkcol - %s\n",
                     (ret == 0 ? "SUCCESS":"FAILURE"));
    }
    return ret;
}

extern int insert_foreignkey(TABFOREIGNKEY_T *fk,
                             COLFOREIGNKEY_T *cols) {
    COLFOREIGNKEY_T *c;
    int              i;
    sqlite3_stmt    *stmt;
    char            *ztail = NULL;
    char             ok = 0;

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
                                " select ltrim(rtrim(?1,'}'),'{'),"
                                "t1.id,lower(?3),t2.id"
                                " from tabTable t1"
                                "   join tabTable t2"
                                "   on t2.varid=t1.varid"
                                " where t1.mwb_id=ltrim(rtrim(?2,'}'),'{')"
                                " and t2.mwb_id=ltrim(rtrim(?4,'}'),'{')"
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
          fprintf(stderr, "No error but foreign key %s not inserted\n",
                          fk->name);
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
        // Try again, without columns
        if (insert_tab == 0) {
          ok = (insert_foreignkey(fk, NULL) != -1); 
        } else {
          ok = 0;
        }
      }
      sqlite3_finalize(stmt);
      c = cols;
      if (debugging()) {
        fprintf(stderr, "  foreign key columns %s.\n",
                        (c ? "known" : "unknown"));
      }
      i = 1;
      while (ok && c) {
        ok = (insert_fkcol(fk, c, i) != -1);
        c = c->next;
        i++;
      }
    }
    if (debugging()) {
      fprintf(stderr, "<< insert_foreignkey - %s\n",
                     (ok ? "SUCCESS":"FAILURE"));
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

static void db_refinit(void) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           i = 0;

    while (G_refddl[i]) {
      if ((sqlite3_prepare_v2(G_db,
                              G_refddl[i], 
                              -1, 
                              &stmt,
                              (const char **)&ztail)  != SQLITE_OK)
          || (sqlite3_step(stmt) != SQLITE_DONE)) {
        fprintf(stderr, "Failure executing:\n%s\n", G_refddl[i]);
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        exit(1);
      }
      sqlite3_finalize(stmt);
      i++;
    }
    i = 1;
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
      // Create additional functions
      (void)setlocale(LC_ALL, "en_US.UTF-8");  
      sqlite3_create_function(G_db, "sqrt", 1, SQLITE_UTF8,
                              0, sql_sqrt, 0, 0);
      sqlite3_create_function(G_db, "levenshtein", 2, SQLITE_UTF8,
                              0, sql_lev, 0, 0);
      db_init();
      rc = 0;
      if (strlen(G_refdb)) {
        sqlite3_stmt *stmt;
        char         *ztail = NULL;
        STRBUF        attach;

        strbuf_init(&attach);
        strbuf_add(&attach, "attach database '");
        strbuf_add(&attach, G_refdb);
        strbuf_add(&attach, "' as ref");
        if ((sqlite3_prepare_v2(G_db, attach.s, -1, &stmt,
                              (const char **)&ztail)  != SQLITE_OK)
            || (sqlite3_step(stmt) != SQLITE_DONE)) {
          fprintf(stderr, "Failure executing:\n%s\n", attach.s);
          fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
          G_refdb[0] = '\0'; // To prevent references
        }
        strbuf_dispose(&attach);
      }
    }
    return rc;
}

extern int db_refconnect(char *ref_file) {
    // Returns 0 if OK, -1 if failure
    int           rc = -1;
    int           ret;

    if (ref_file) {
      ret = sqlite3_open(ref_file, &G_db);
      if (ret == SQLITE_OK) {
        db_refinit();
        strncpy(G_refdb, ref_file, FILENAME_MAX);
        G_ref = 1;
        rc = 0;
      } else {
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
      }
    }
    return rc;
}

extern void db_disconnect(void) {
    if (G_db) {
      if ((G_ref == 0)
          && strlen(G_refdb)) {
        sqlite3_stmt *stmt;
        char         *ztail = NULL;

        if ((sqlite3_prepare_v2(G_db, "detach database ref", -1, &stmt,
                              (const char **)&ztail)  != SQLITE_OK)
            || (sqlite3_step(stmt) != SQLITE_DONE)) {
          fprintf(stderr, "Failure executing detach\n");
          fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        }
      }
      sqlite3_close(G_db);
      G_db = NULL;
      if (G_ref) {
        G_ref = 0;
      }
    }
}

extern int db_runcheck(short check_code, char report, char *query, int varid) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           rowcnt = -1;
    int           rc;
    char          num = 0;
    char          pct = 0;
    int           num_val = -1;
    int           to_bind;

    if (G_db && query) {
      if (debugging()) {
        printf("Running check %s\n", grad_keyword(check_code));
      }
      if (((pct = (strncmp(grad_keyword(check_code),
                            "percent_", 8) == 0)) != 0)
         || (strncmp(grad_keyword(check_code), "number_", 7) == 0)) {
        num = 1;
      }
      _must_succeed("run check",
                    sqlite3_prepare_v2(G_db,
                                       query,
                                       -1, 
                                       &stmt,
                                       (const char **)&ztail));
      // Depending on the query there may be to
      // bind "varid"
      to_bind = sqlite3_bind_parameter_count(stmt);
      if (to_bind) {
        if (sqlite3_bind_int(stmt, 1, varid) != SQLITE_OK) {
          fprintf(stderr, "Failure binding:\n%d\n", varid);
          fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
          // Let it fail ...
        }
      }
      rowcnt = 0;
      while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (num) {
          num_val = sqlite3_column_int(stmt, 0);
        }
        if (report) {
          // The query is expected to only return a
          // list of names. It could be checked dynamically.
          // Carriage return added with comment by caller.
          if (num) {
            printf("\t%d%s", num_val, (pct ? "%" : ""));
          } else {
            printf("\t%s", sqlite3_column_text(stmt, 0));
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
    return (num ? num_val : rowcnt);
}

extern int db_refcheck(short check_code, char *query, int varid) {
    // Run a check against a reference model.
    // Always returns a number.
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           num = -1;
    int           rc;

    if (G_db && query) {
      if (debugging()) {
        printf("Running refcheck %s\n", grad_keyword(check_code));
      }
      _must_succeed("run refcheck",
                    sqlite3_prepare_v2(G_db,
                                       query,
                                       -1, 
                                       &stmt,
                                       (const char **)&ztail));
      if (sqlite3_bind_int(stmt, 1, varid) != SQLITE_OK) {
        fprintf(stderr, "Failure binding when checking reference\n");
        fprintf(stderr, "%s (err code: %d, extended: %d)\n",
                        sqlite3_errmsg(G_db),
                        sqlite3_errcode(G_db),
                        sqlite3_extended_errcode(G_db));
        sqlite3_finalize(stmt);
        return 0;  // Assume it passes ...
      }
      rc = sqlite3_step(stmt);
      if (rc == SQLITE_ROW) {
        num = sqlite3_column_int(stmt, 0);
      } else if (rc == SQLITE_DONE) {
        num = 0;
      } else {
        fprintf(stderr, "Query: %s\n", query);
        fprintf(stderr, "%s (err code: %d, extended: %d)\n",
                        sqlite3_errmsg(G_db),
                        sqlite3_errcode(G_db),
                        sqlite3_extended_errcode(G_db));
        num = 0; // Assume it passes
      }
      sqlite3_finalize(stmt);
    }
    return num;
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
                             " left outer join tabFKcol fc"
                             " on fc.fkid=f.fkid"
                             " left outer join tabcolumn c"
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

extern short best_variant(int *pbase_grade, int *pdpct, int *ptpct) {
    // Number of managed pieces of data, that is including whatever
    // is system-generated and not counting columns involved in FK
    // relationships several times
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           rc;
    short         ret = 0;

    if (G_db && pbase_grade && pdpct && ptpct) {
      rc = sqlite3_prepare_v2(G_db,
                              "select x.varid,v.max_points,"
                              "x.dpct,x.tpct"
                              " from (select a.varid,"
                              "   0.7*abs(a.dcount-b.dcount)"
                              "  +0.3* abs(a.tcount-b.tcount) w,"
                              "  round(100*b.dcount/a.dcount) dpct,"
                              "  round(100*b.tcount/a.tcount) tpct"
                              "  from (select t.varid,"
                              "   count(distinct c.tabid) tcount,"
                              "   count(*) as dcount"
                              "   from ref.tabTable t"
                              "     join ref.tabColumn c"
                              "     on c.tabid=t.id"
                              "     left outer join ref.tabForeignKey fk"
                              "     on fk.tabid=c.tabid"
                              "     left outer join ref.tabFKcol as fkc"
                              "     on fkc.fkid=fk.id"
                              "    and fkc.colid=c.id"
                              "   where c.autoInc=0"
                              "    and fkc.colid is null"
                              "   group by t.varid) a"
                              "  cross join (select count(distinct c.tabid) tcount,"
                              "     count(*) as dcount"
                              "    from main.tabColumn c"
                              "      left outer join main.tabForeignKey fk"
                              "      on fk.tabid=c.tabid"
                              "      left outer join main.tabFKcol as fkc"
                              "      on fkc.fkid=fk.id"
                              "     and fkc.colid=c.id"
                              "    where c.autoInc=0"
                              "     and fkc.colid is null) b"
                              "  order by w"
                              "  limit 1) x"
                              "  join ref.tabVariant v"
                              "  on v.id=x.varid",
                              -1, 
                              &stmt,
                              (const char **)&ztail);
      if (rc != SQLITE_OK) {
        fprintf(stderr, "Failure parsing best variant query\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        return -1;
      }
      if ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ret = (short)sqlite3_column_int(stmt, 0);
        *pbase_grade = sqlite3_column_int(stmt, 1);
        *pdpct = sqlite3_column_int(stmt, 2);
        *ptpct = sqlite3_column_int(stmt, 3);
      } else {
        fprintf(stderr, "Failure executing best variant query\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        ret = -1;
      }
      sqlite3_finalize(stmt);
    }
    return ret;
}

extern int schema_matching(short varid) {
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    sqlite3_stmt *stmt2;
    char         *ztail2 = NULL;
    int           rc;
    int           previd;
    int           id;
    int           refid;
    char          ref_set;
    double        distance = 0;
    double        fulldist = 0;
    int           ret = -1;

    if (G_db && (varid > 0)) {
      rc = sqlite3_prepare_v2(G_db,
           "select x.tabid,y.tabid,levenshtein(x.name,y.name) namedist,"
           "sqrt((x.cols-y.cols)*(x.cols-y.cols)"
           "+(x.datatypes-y.datatypes)*(x.datatypes-y.datatypes)"
           "+(x.fk_out-y.fk_out)*(x.fk_out-y.fk_out)"
           "+(x.fk_in-y.fk_in)*(x.fk_in-y.fk_in)) dist"
           " from (select a.tabid,t.name,a.cols,a.datatypes,"
           "  coalesce(b.fk_out,0) fk_out,coalesce(c.fk_in,0) fk_in"
           " from (select tabid,count(*) cols,"
           "  count(distinct datatype) datatypes"
           "  from main.tabColumn"
           "  where autoinc=0"
           "  group by tabid) a"
           " join main.tabTable t"
           " on t.id=a.tabid"
           " left outer join (select tabid,count(*) fk_out"
           "  from main.tabForeignKey"
           "  group by tabid) b"
           " on b.tabid=a.tabid"
           " left outer join (select reftabid,count(*) fk_in"
           "  from main.tabForeignKey"
           "  group by reftabid) c"
           " on c.reftabid=a.tabid) x"
           " cross join(select a.tabid,t.name,a.cols,"
           "  a.datatypes,coalesce(b.fk_out,0) fk_out,"
           "  coalesce(c.fk_in,0) fk_in"
           " from ref.tabTable t"
           "  join (select tabid,count(*) cols,"
           "   count(distinct datatype) datatypes"
           "   from ref.tabColumn"
           "   where autoinc=0"
           "   group by tabid) a"
           "  on a.tabid=t.id"
           "  left outer join (select tabid,count(*) fk_out"
           "   from ref.tabForeignKey"
           "   group by tabid) b"
           "  on b.tabid=a.tabid"
           "  left outer join (select reftabid,count(*) fk_in"
           "   from ref.tabForeignKey"
           "   group by reftabid) c"
           "  on c.reftabid=a.tabid"
           " where t.varid=?1) y"
           " order by x.tabid,dist,namedist",
                              -1, 
                              &stmt,
                              (const char **)&ztail);
      if (rc != SQLITE_OK) {
        fprintf(stderr, "Failure parsing matching query\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        return ret;
      }
      if (sqlite3_bind_int(stmt, 1, varid) != SQLITE_OK) {
        fprintf(stderr, "Failure binding matching query\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        return ret;
      }
      rc = sqlite3_prepare_v2(G_db,
                              "update main.tabTable"
                              " set refid=?1"
                              " where id=?2",
                              -1,
                              &stmt2,
                              (const char **)&ztail2);
      if (rc != SQLITE_OK) {
        fprintf(stderr, "Failure parsing matching update statement\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        sqlite3_finalize(stmt);
        return ret;
      }
      previd = -1;
      ref_set = 0;
      (void)db_begin_tx();
      while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if ((id = sqlite3_column_int(stmt, 0)) != previd) {
          previd = id;
          ref_set = 0;
        }
        if (!ref_set) {
          refid = sqlite3_column_int(stmt, 1);
          // Bind to the update statement
          if ((sqlite3_bind_int(stmt2, 1, refid) != SQLITE_OK)
             || (sqlite3_bind_int(stmt2, 2, id) != SQLITE_OK)) {
            fprintf(stderr, "Failure binding matching update statement\n");
            fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
            (void)db_rollback();
            sqlite3_finalize(stmt);
            sqlite3_finalize(stmt2);
            return ret;
          }
          if (sqlite3_step(stmt2) == SQLITE_DONE) {
            ref_set = 1;
            distance += sqlite3_column_double(stmt, 3);
          } else {
            // Unique constraint violation OK
            if ((sqlite3_errcode(G_db) != SQLITE_CONSTRAINT)
               || (sqlite3_extended_errcode(G_db)
                    != SQLITE_CONSTRAINT_UNIQUE)) {
              fprintf(stderr, "Failure executing matching update statement\n");
              fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
              (void)db_rollback();
              sqlite3_finalize(stmt);
              sqlite3_finalize(stmt2);
              return ret;
            }
          }
          sqlite3_reset(stmt2);
          sqlite3_clear_bindings(stmt2);
        }
      }
      if (rc == SQLITE_DONE) {
        (void)db_commit();
      } else {
        fprintf(stderr, "Failure executing matching statement\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        (void)db_rollback();
        sqlite3_finalize(stmt);
        sqlite3_finalize(stmt2);
        return ret;
      }
      sqlite3_finalize(stmt);
      sqlite3_finalize(stmt2);
      // We have matched whatever we could.
      // Now we must finalize the computation of the distance
      // between the submitted model and the reference.
      rc = sqlite3_prepare_v2(G_db,
           " select sum(fulldist),sum(missing)" 
           " from (select 0 as fulldist,"
           "  sum(sqrt(a.cols*a.cols+a.datatypes*a.datatypes"
           " +coalesce(b.fk_out*b.fk_out,0)+coalesce(c.fk_in*c.fk_in,0)))"
           " as missing"
           " from main.tabTable t"
           "  join (select tabid,count(*) cols,"
           "  count(distinct datatype) datatypes"
           "  from main.tabColumn"
           "  where autoinc=0"
           "  group by tabid) a"
           " on a.tabid=t.id"
           " left outer join (select tabid,count(*) fk_out"
           "  from main.tabForeignKey"
           "  group by tabid) b"
           " on b.tabid=a.tabid"
           " left outer join (select reftabid,count(*) fk_in"
           "  from main.tabForeignKey"
           "  group by reftabid) c"
           " on c.reftabid=a.tabid"
           " where t.refid is null"
           " union all"
           " select sum(sqrt(a.cols*a.cols+a.datatypes*a.datatypes"
           " +coalesce(b.fk_out*b.fk_out,0)+coalesce(c.fk_in*c.fk_in,0)))"
           " as fulldist,"
           " sum(case when t2.refid is null then"
           "  sqrt(a.cols*a.cols+a.datatypes*a.datatypes"
           " +coalesce(b.fk_out*b.fk_out,0)+coalesce(c.fk_in*c.fk_in,0))"
           " else 0 end) as missing"
           " from ref.tabTable t"
           "  join (select tabid,count(*) cols,"
           "   count(distinct datatype) datatypes"
           "   from ref.tabColumn"
           "   where autoinc=0"
           "   group by tabid) a"
           "  on a.tabid=t.id"
           "  left outer join (select tabid,count(*) fk_out"
           "   from ref.tabForeignKey"
           "   group by tabid) b"
           "  on b.tabid=a.tabid"
           "  left outer join (select reftabid,count(*) fk_in"
           "   from ref.tabForeignKey"
           "   group by reftabid) c"
           "  on c.reftabid=a.tabid"
           "  left outer join main.tabTable t2"
           "  on t2.refid=t.id"
           " where t.varid=?1)",
                              -1, 
                              &stmt,
                              (const char **)&ztail);
      if (rc != SQLITE_OK) {
        fprintf(stderr, "Failure parsing matching query 2\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        sqlite3_finalize(stmt);
        return ret;
      }
      if (sqlite3_bind_int(stmt, 1, varid) != SQLITE_OK) {
        fprintf(stderr, "Failure binding matching query 2\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
        sqlite3_finalize(stmt);
        return ret;
      }
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        fulldist = sqlite3_column_double(stmt, 0);
        distance += sqlite3_column_double(stmt, 1);
        ret = (int)(100 * (float)(fulldist - distance) / fulldist); 
      } else {
        fprintf(stderr, "Failure binding matching query 2\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
      }
      sqlite3_finalize(stmt);
    }
    return ret;
}

extern void get_variant_tests(short varid, short *checks, int check_cnt) {
    int           i;
    sqlite3_stmt *stmt;
    char         *ztail = NULL;
    int           rc;

    if (checks) {
      for (i = 0; i < check_cnt; i++) {
        checks[i] = 1;
      }
      if ((sqlite3_prepare_v2(G_db,
                                "select testid,pass"
                                " from ref.tabVariantTest"
                                " where varid=?1",
                                -1, 
                                &stmt,
                                (const char **)&ztail) != SQLITE_OK)
         || (sqlite3_bind_int(stmt, 1, varid) != SQLITE_OK)) {
        fprintf(stderr, "Failure preparing/binding get_variant_tests\n");
        fprintf(stderr, "%s (err code: %d, extended: %d\n",
                        sqlite3_errmsg(G_db),
                        sqlite3_errcode(G_db),
                        sqlite3_extended_errcode(G_db));
        sqlite3_finalize(stmt);
        return;
      }
      while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        i = sqlite3_column_int(stmt, 0);
        if ((i >= 0) && (i < check_cnt)) {
          checks[i] = sqlite3_column_int(stmt, 1);
        }
      }
      if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failure binding matching query 2\n");
        fprintf(stderr, "%s\n", sqlite3_errmsg(G_db));
      }
      sqlite3_finalize(stmt);
    }
}
