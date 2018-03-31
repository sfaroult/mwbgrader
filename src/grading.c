/* ============================================================== *
 *
 *    grading.c
 *
 *  All the grading intelligence is here.
 *
 * ============================================================== */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "grad.h"
#include "dbop.h"
#include "debug.h"
#include "grading.h"


#define LINE_LEN   250

// Rules for grading
#define NO_RULE       -1
#define ASSIGN         0
#define MULT_ONCE      1
#define MULT_EACH      2
#define SUB_ONCE       4
#define SUB_EACH       8
#define ADD_ONCE      16
#define ADD_EACH      32
#define AT_LEAST      64
#define AT_MOST      128

#define _trim(s)     {int __len; if (s) {__len = strlen(s);\
                      while (__len && isspace(s[__len-1])){__len--;}\
                      s[__len] = '\0';}}
#define _skip_spaces(s) {if (s) {while (isspace(*s)) {s++;}}}
#define _max(a, b)      ((a > b) ? a : b)
#define _min(a, b)      ((a > b) ? b : a)

typedef struct overview {
               int             tabcnt;
               int             datacnt;
               RELATIONSHIP_T *rel;
               NAME_ITEM_T    *selfref;
             } OVERVIEW_T;

typedef struct {
          short check_code;
          char *description;
          char *sql;  // Query applied to the submission
          char *sql2; // Query applied to the reference model (always a count)
          short rule;
          float threshold;
          float val;
          short cap;  // Maximum number of times points are taken off
                      // or added, for rules that apply to each occurrence
          short next; // "Pointer" - index of the next control to run
         } GRADING_T;

extern int levenshtein(char *s1, char *s2);

// Initialized structure
// If no grading.conf file apply default.
// grading.conf can change the rules

static short     G_chain_start = -1;
static short     G_model_weight = MODEL_WEIGHT;
// Default control sequence
static short     G_grading_seq[GRAD_COUNT] =
                  {GRAD_START_GRADE,
                   GRAD_NUMBER_OF_TABLES,
                   GRAD_NUMBER_OF_INFO_PIECES,
                   GRAD_NO_PK,
                   GRAD_EVERYTHING_NULLABLE,
                   GRAD_NO_UNIQUENESS,
                   GRAD_ISOLATED_TABLES,
                   GRAD_CIRCULAR_FK,
                   GRAD_MULTIPLE_LEGS,
                   GRAD_ONE_ONE_RELATIONSHIP,
                   GRAD_TOO_MANY_FK_TO_SAME,
                   GRAD_SINGLE_COL_TABLE,
                   GRAD_ULTRA_WIDE,
                   GRAD_USELESS_AUTOINC,
                   GRAD_SAME_DATATYPE_FOR_ALL_COLS,
                   GRAD_SAME_VARCHAR_LENGTH,
                   GRAD_PERCENT_SINGLE_COL_IDX,
                   GRAD_REDUNDANT_INDEXES,
                   GRAD_PERCENT_COMMENTED_TABLES,
                   GRAD_PERCENT_COMMENTED_COLUMNS};

static GRADING_T G_grading[] =
            {{GRAD_EVERYTHING_NULLABLE,
              "No mandatory column other than possibly a system-generated one",
              "select t.name"
              " from tabTable t"
              "      join tabColumn c"
              "        on c.tabid=t.id"
              " where c.autoinc='0'"
              " and t.name not like '{%'"
              " group by t.name"
              " having max(c.IsNotNull)='0'"
              " order by 1",
              "select count(*)"
              " from tabTable t"
              "      join tabColumn c"
              "        on c.tabid=t.id"
              " where c.autoinc='0'"
              " and t.name not like '{%'"
              " and t.varid=?1"
              " group by t.name"
              " having max(c.IsNotNull)='0'",
              SUB_EACH,
              0,
              5.0,
              999,
              -1},
             {GRAD_NUMBER_OF_TABLES,
              "Required number of tables in the schema",
              "select count(*) from tabTable"
              " where name not like '{%'",
              "select count(*) from tabTable where varid=?1"
              " and name not like '{%'",
              SUB_ONCE | AT_LEAST,
              4,
              15.0,
              999,
              -1},
             {GRAD_NUMBER_OF_INFO_PIECES,
              "Required number of information pieces in the schema",
              "select count(*)"
              " from tabColumn c"
              " left outer join tabForeignKey fk"
              " on fk.tabid=c.tabid"
              " left outer join tabFKcol as fkc"
              " on fkc.fkid=fk.id"
              " and fkc.colid=c.id"
              " where c.autoInc=0"
              " and fkc.colid is null",
              "select count(*)"
              " from tabColumn c"
              " join tabTable t"
              " on t.id=c.tabid"
              " left outer join tabForeignKey fk"
              " on fk.tabid=c.tabid"
              " left outer join tabFKcol as fkc"
              " on fkc.fkid=fk.id"
              " and fkc.colid=c.id"
              " where c.autoInc=0"
              " and t.varid=?1"
              " and t.name not like '{%'"
              " and fkc.colid is null",
              SUB_ONCE | AT_LEAST,
              10,
              5.0,
              999,
              -1},
             {GRAD_ISOLATED_TABLES,
              "Tables not involved into any FK relationship",
              "select t.name"
              " from tabTable t"
              "  left outer join"
              "  (select distinct"
              "     case x.n"
              "       when 1 then fk.tabid"
              "       when 2 then fk.reftabid"
              "     end as tabid"
              " from tabForeignKey fk"
              "   cross join (select 1 as n"
              "    union select 2) x) y"
              " on y.tabid=t.id"
              " where y.tabid is null"
              " and t.name not like '{%'"
              " order by 1",
              "select count(*)"
              " from tabTable t"
              "  left outer join"
              "  (select distinct"
              "     case x.n"
              "       when 1 then fk.tabid"
              "       when 2 then fk.reftabid"
              "     end as tabid"
              " from tabForeignKey fk"
              "   cross join (select 1 as n"
              "    union select 2) x) y"
              " on y.tabid=t.id"
              " where y.tabid is null"
              " and t.name not like '{%'"
              " and t.varid=?1",
              SUB_EACH,
              0,
              3.0,
              999,
              -1},
             {GRAD_TOO_MANY_FK_TO_SAME,
              "Tables with more than two FKs to the same table",
              "select t1.name as TabName,"
              "  t2.name as RefTabName,"
              "  x.cnt as fk_count"
              " from (select tabid,reftabid,count(*) cnt"
              "  from tabForeignKey"
              "  group by tabid,reftabid"
              " having count(*)>2) x"
              " join tabTable t1"
              "  on t1.id=x.tabid"
              " join tabTable t2"
              "  on t2.id=x.reftabid"
              " where t1.name not like '{%'"
              " and t2.name not like '{%'"
              " order by 1",
              "select count(*)"
              " from (select tabid,reftabid,count(*) cnt"
              "  from tabForeignKey"
              "  group by tabid,reftabid"
              " having count(*)>2) x"
              " join tabTable t1"
              "  on t1.id=x.tabid"
              " join tabTable t2"
              "  on t2.id=x.reftabid"
              " where t1.varid=?1"
              " and t2.name not like '{%'"
              " and t1.name not like '{%'"
              " and t2.varid=t1.varid",
              SUB_EACH,
              0,
              1.5,
              999,
              -1},
             {GRAD_SINGLE_COL_TABLE,
              "Tables with a single column",
              "select t.name"
              " from tabTable t"
              "  join tabColumn c"
              "  on c.tabid=t.id"
              " where t.name not like '{%'"
              " group by t.name"
              " having count(c.id)=1"
              " order by 1",
              "select count(*)"
              " from(select t.name"
              "  from tabTable t"
              "   join tabColumn c"
              "   on c.tabid=t.id"
              "  where t.varid=?1"
              "  and t.name not like '{%'"
              "  group by t.name"
              "  having count(c.id)=1) x",
              SUB_EACH,
              0,
              3.5,
              999,
              -1},
             {GRAD_PERCENT_SINGLE_COL_IDX,
              "Single column indexes as a percentage",
              // Percentage of single-column indexes
              "select round(100*cast(sum(single_col_idx) as float)"
              " / count(*)) as pct_one_col"
              " from (select case max(seq)"
              "   when 1 then 1"
              "   else 0"
              " end as single_col_idx,"
              " idxid"
              " from tabIndexCol"
              " group by idxid) x",
              "select round(100*cast(sum(single_col_idx) as float)"
              " / count(*)) as pct_one_col"
              " from (select case max(ic.seq)"
              "   when 1 then 1"
              "   else 0"
              " end as single_col_idx,"
              " ic.idxid"
              " from tabIndexCol ic"
              "  join tabIndex i"
              "  on i.id=ic.idxid"
              "  join tabTable t"
              "  on t.id=i.tabid"
              " where t.varid=?1"
              " and t.name not like '{%'"
              " group by  ic.idxid) x",
              SUB_ONCE | AT_MOST,
              99,
              5.0,
              999,
              -1},
             {GRAD_SAME_VARCHAR_LENGTH,
              "All varchars have a default 'just in case' value",
              // All varchar columns have the same (default or "safe") length
              "select 'Length:' || cast(max(collength) as char) collength"
              " from tabColumn"
              " where datatype='varchar'"
              " group by datatype"
              " having count(distinct collength) = 1",
              "select 1"
              " from tabColumn c"
              " join tabTable t"
              " on t.id=c.tabid"
              " where c.datatype='varchar'"
              " and t.varid=?1"
              " and t.name not like '{%'"
              " group by c.datatype"
              " having count(distinct c.collength) = 1",
              SUB_ONCE,
              0,
              5.0,
              999,
              -1},
             {GRAD_SAME_DATATYPE_FOR_ALL_COLS,
              "Tables where all columns look like default varchar columns",
              // Tables where all columns have the same varchar datatype
              // except perhaps an autoincrement column
              "select t.name as TableName"
              " from (select tabid,max(datatype) datatype,"
              "max(collength) collength"
              " from tabColumn"
              " where autoinc=0"
              " group by tabid"
              " having count(*)>1"
              " and count(distinct datatype||cast(collength as char))=1) x"
              " join tabTable t"
              " on t.id=x.tabid"
              " where datatype='varchar'"
              " and t.name not like '{%'"
              " order by 1",
              "select count(*)"
              " from (select tabid,max(datatype) datatype,"
              "max(collength) collength"
              " from tabColumn"
              " where autoinc=0"
              " group by tabid"
              " having count(*)>1"
              " and count(distinct datatype||cast(collength as char))=1) x"
              " join tabTable t"
              " on t.id=x.tabid"
              " where x.datatype='varchar'"
              " and t.name not like '{%'"
              " and t.varid=?1",
              SUB_ONCE,
              0,
              3.5,
              999,
              -1},
             {GRAD_REDUNDANT_INDEXES,
              "Single-column indexes made useless by a multiple column index",
              "select t.name||': '||one_col.redundant_idx"
              "||' redundant with '||i2.name"
              " from (select i.name redundant_idx,"
              " i.tabid,i.id as idxid,max(ic.colid) as colid"
              " from tabIndex i"
              " join tabIndexCol ic"
              " on ic.idxid=i.id"
              " group by i.name,i.tabid,i.id"
              " having max(ic.seq)=1) one_col"
              " join tabIndex i2"
              " on i2.tabid=one_col.tabid"
              " and i2.id<>one_col.idxid"
              " join tabIndexCol ic2"
              " on ic2.idxid=i2.id"
              " and ic2.colid=one_col.colid"
              " and ic2.seq=1"
              " join tabTable t"
              " on t.id=i2.tabid"
              " where t.name not like '{%'"
              " order by 1",
              "select count(*)" 
              " from (select i.name redundant_idx,"
              " i.tabid,i.id as idxid,max(ic.colid) as colid"
              " from tabIndex i"
              " join tabIndexCol ic"
              " on ic.idxid=i.id"
              " group by i.name,i.tabid,i.id"
              " having max(ic.seq)=1) one_col"
              " join tabIndex i2"
              " on i2.tabid=one_col.tabid"
              " and i2.id<>one_col.idxid"
              " join tabIndexCol ic2"
              " on ic2.idxid=i2.id"
              " and ic2.colid=one_col.colid"
              " and ic2.seq=1"
              " join tabTable t"
              " on t.id=i2.tabid"
              " where t.varid=?1"
              " and t.name not like '{%'",
              SUB_EACH,
              0,
              2.0,
              999,
              -1},
             {GRAD_NO_PK,
              "Tables without a primary (or unique) key",
              "select t.name as TableName"
              " from TabTable t"
              " left outer join"
              " (select distinct tabid"
              "   from tabIndex"
              "   where isPrimary=1 or isUnique=1) has_pk"
              " on has_pk.tabid=t.id"
              " where has_pk.tabid is null"
              " and t.name not like '{%'"
              " order by 1",
              NULL,
              SUB_EACH,
              0,
              10.0,
              999,
              -1},
             {GRAD_ONE_ONE_RELATIONSHIP,
              "Tables in a one-to-one relationship that"
              " doesn't look like inheritance",
              // One-one relationship if all columns in a unique or primary
              // index also belongs to a (single) fk 
              "select t1.name"
              "||' in a one-to-one relationship with '||"
              " t2.name"
              " from (select xx.tabid,xx.reftabid"
              "  from (select i.tabid,fk.reftabid"
              "  from tabIndex i"
              "   join tabIndexCol ic"
              "    on ic.idxid=i.id"
              "  left outer join tabForeignKey fk"
              "    on fk.tabid=i.tabid"
              "   and fk.tabid<>fk.reftabid"
              "  left outer join tabFkcol fkc"
              "    on fkc.fkid=fk.id"
              "   and ic.colid=fkc.colid"
              " where (i.isPrimary=1 or i.isUnique=1)"
              " group by i.tabid,fk.reftabid,fk.id,i.id"
              " having count(ic.colid)=count(fkc.colid)) xx"
              // Exclude what could reasonably look like inheritance
              " left outer join"
              " (select y.reftabid as parent_table,"
              "   ','||group_concat(y.tabid)||',' as child_tables"
              " from (select tabid,pkcols,fkcols,fkid,reftabid"
              "  from (select pk.tabid,count(*) as pkcols,"
              "   sum(case"
              "    when fkc.colid is null then 0"
              "    else 1"
              "    end) as fkcols,fk.id as fkid,fk.reftabid"
              " from tabIndex pk"
              "  join tabIndexCol pkc"
              "  on pkc.idxid=pk.id"
              "  left outer join tabForeignKey fk"
              "  on fk.tabid=pk.tabid"
              "  and fk.tabid<>fk.reftabid"
              "  left outer join tabFkcol fkc"
              "  on fkc.fkid=fk.id"
              "  and pkc.colid=fkc.colid"
              " where pk.isPrimary=1"
              " group by pk.tabid,fk.id,fk.reftabid) x"
              // All the PK columns are in a single FK
              " where pkcols=fkcols) y"
              " group by y.reftabid"
              " having count(*)>1) zz"
              " on zz.parent_table=xx.reftabid"
              " and zz.child_tables like '%,'||xx.tabid||',%'"
              " where zz.parent_table is null) one2one"
              " join tabTable t1"
              " on t1.id=one2one.tabid"
              " join tabTable t2"
              " on t2.id=one2one.reftabid"
              " where t1.name not like '{%'"
              " and t2.name not like '{%'"
              " order by 1",
              "select count(*)"
              " from (select xx.tabid,xx.reftabid"
              "  from (select i.tabid,fk.reftabid"
              "  from tabIndex i"
              "   join tabIndexCol ic"
              "    on ic.idxid=i.id"
              "  left outer join tabForeignKey fk"
              "    on fk.tabid=i.tabid"
              "   and fk.tabid<>fk.reftabid"
              "  left outer join tabFkcol fkc"
              "    on fkc.fkid=fk.id"
              "   and ic.colid=fkc.colid"
              " where (i.isPrimary=1 or i.isUnique=1)"
              " group by i.tabid,fk.reftabid,fk.id,i.id"
              " having count(ic.colid)=count(fkc.colid)) xx"
              // Exclude what could reasonably look like inheritance
              " left outer join"
              " (select y.reftabid as parent_table,"
              "   ','||group_concat(y.tabid)||',' as child_tables"
              " from (select tabid,pkcols,fkcols,fkid,reftabid"
              "  from (select pk.tabid,count(*) as pkcols,"
              "   sum(case"
              "    when fkc.colid is null then 0"
              "    else 1"
              "    end) as fkcols,fk.id as fkid,fk.reftabid"
              " from tabIndex pk"
              "  join tabIndexCol pkc"
              "  on pkc.idxid=pk.id"
              "  left outer join tabForeignKey fk"
              "  on fk.tabid=pk.tabid"
              "  and fk.tabid<>fk.reftabid"
              "  left outer join tabFkcol fkc"
              "  on fkc.fkid=fk.id"
              "  and pkc.colid=fkc.colid"
              " where pk.isPrimary=1"
              " group by pk.tabid,fk.id,fk.reftabid) x"
              // All the PK columns are in a single FK
              " where pkcols=fkcols) y"
              " group by y.reftabid"
              " having count(*)>1) zz"
              " on zz.parent_table=xx.reftabid"
              " and zz.child_tables like '%,'||xx.tabid||',%'"
              " where zz.parent_table is null) one2one"
              " join tabTable t1"
              " on t1.id=one2one.tabid"
              " join tabTable t2"
              " on t2.id=one2one.reftabid"
              " where t1.varid=?1"
              " and t1.name not like '{%'"
              " and t2.name not like '{%'"
              " and t2.varid=t1.varid",
              SUB_EACH,
              0,
              3.0,
              999,
              -1},
             {GRAD_NO_UNIQUENESS,
              "Tables with no other unique columns"
              " than possibly a system-generated id",
              // Check that every table has a unique or primary index,
              // once you have taken off auto increment columns
              "select t.name as TableName"
              " from tabTable t"
              " left outer join"
              // Tables that have another PK or unique index than an index
              // on an autoincrement column 
              " (select distinct i.tabid"
              "  from tabIndex i"
              " join (select ic.idxid"     // Indexes that DON'T contain
              "   from tabIndexCol ic"     // an autoincrement column
              "    join tabColumn c"
              "    on c.id=ic.colid"
              "  group by ic.idxid"
              "  having max(c.autoInc)='0') x"
              "  on x.idxid=i.id"
              " where i.isprimary=1"
              "  or i.isunique=1) y"
              " on y.tabid=t.id"
              " where y.tabid is null"
              " and t.name not like '{%'"
              " order by 1",
              NULL,
              SUB_EACH,
              0,
              4.0,
              999,
              -1},
             {GRAD_PERCENT_COMMENTED_TABLES,
              "Percentage of tables with comments",
              "select round(100.0*sum(case comment_len when 0"
              " then 0 else 1 end)/count(*)) ptc_commented"
              " from tabTable",
              "select round(100.0*sum(case comment_len when 0"
              " then 0 else 1 end)/count(*)) ptc_commented"
              " from tabTable"
              " where varid=?1"
              " and name not like '{%'",
              SUB_ONCE | AT_LEAST,
              50,
              5.0,
              999,
              -1},
             {GRAD_MULTIPLE_LEGS,
              "Three-legged (or more) many-to-many relationships",
              // Multiple legs relationships
              "select t.name||': '||cast(x.legs as char)||' legs'"
              " from (select ut.tabid,count(*) as legs"
              " from (select t.id as tabid"
              "  from tabTable t"
              "  left outer join tabForeignKey fk"
              "   on fk.reftabid=t.id"
              " where fk.id is null) ut" // Unreferenced tables
              " join tabForeignKey fk"
              " on fk.tabid=ut.tabid"
              " group by ut.tabid"
              " having count(*)>2) x"
              " join tabTable t"
              " on t.id=x.tabid"
              " where t.name not like '{%'"
              " order by 1",
              "select count(*)"
              " from (select ut.tabid,count(*) as legs"
              " from (select t.id as tabid"
              "  from tabTable t"
              "  left outer join tabForeignKey fk"
              "   on fk.reftabid=t.id"
              " where fk.id is null) ut" // Unreferenced tables
              " join tabForeignKey fk"
              " on fk.tabid=ut.tabid"
              " group by ut.tabid"
              " having count(*)>2) x"
              " join tabTable t"
              " on t.id=x.tabid"
              " where t.varid=?1"
              " and t.name not like '{%'",
              SUB_EACH,
              0,
              2.5,
              999,
              -1},
             {GRAD_PERCENT_COMMENTED_COLUMNS,
              "Percentage of columns with comments",
              "select round(100.0*sum(case comment_len when 0"
              " then 0 else 1 end)/count(*)) ptc_commented"
              " from tabColumn",
              "select round(100.0*sum(case c.comment_len when 0"
              " then 0 else 1 end)/count(*)) ptc_commented"
              " from tabColumn c"
              " join tabTable t"
              " on t.id=c.tabid"
              " where t.varid=?1"
              " and t.name not like '{%'",
              SUB_ONCE | AT_LEAST,
              10,
              5.0,
              999,
              -1},
             {GRAD_CIRCULAR_FK,
              "Presence of circular foreign keys",
              // Detection of circular FKs
              "with q as (select coalesce(name,id) as id,reftabid as tabid"
              " from tabForeignKey"
              " where tabid<>reftabid"
              " union all"
              " select q.id,fk.reftabid"
              " from tabForeignKey fk"
              " join q"
              " on q.tabid=fk.tabid"
              " where fk.tabid<>fk.reftabid)"
              " select id"
              " from (select *"
              " from q"
              " limit 5000) x"
              " group by id"
              " having count(*) > 100",
              NULL,
              SUB_ONCE,
              0,
              10.0,
              999,
              -1},
             {GRAD_START_GRADE,
              "Initial grade from which the final grade is computed",
              NULL,
              NULL,
              ASSIGN,
              0,
              100.0,
              999,
              -1},
             {GRAD_ULTRA_WIDE,
              "Tables with a number of columns far above other tables",
              "select t.name||': '||cast(count(c.id) as char)||' columns'"
              " from tabTable t"
              " join tabColumn c"
              " on c.tabid=t.id"
              " where t.name not like '{%'"
              " group by t.name"
              " having count(c.id)>(select 3*sum(cols)/count(tabid)"
              "  from (select tabid,count(*) as cols"
              "   from tabColumn"
              "   group by tabid"
              "   having count(*)>3))" // Eliminate label tables
              " order by 1",
              "select count(*)"
              " from tabTable t"
              " join tabColumn c"
              " on c.tabid=t.id"
              " where t.varid=?1"
              " and t.name not like '{%'"
              " group by t.name"
              " having count(c.id)>(select 3*sum(cols)/count(tabid)"
              "  from (select c.tabid,count(*) as cols"
              "   from tabColumn c"
              "    join tabTable t"
              "    on t.id=c.tabid"
              "   where t.varid=?1"
              "   group by c.tabid"
              "   having count(*)>3))" // Eliminate label tables
              " order by 1",
              SUB_EACH,
              0,
              1.0,
              999,
              -1},
             {GRAD_USELESS_AUTOINC,
              "Unreferenced tables with a system-generated row identifier",
              // Unreferenced tables with (useless) autoincrement columns
              "select ut.name"
              " from (select t.name,t.id as tabid"
              " from tabTable t"
              " left outer join tabForeignKey fk"
              " on fk.reftabid=t.id"
              " where fk.id is null"
              " and t.name not like '{%') ut" // Unreferenced tables
              " join tabColumn c"
              " on c.tabid=ut.tabid"
              " and c.autoinc=1"
              " order by 1",
              "select count(*)"
              " from (select t.name,t.id as tabid"
              " from tabTable t"
              " left outer join tabForeignKey fk"
              " on fk.reftabid=t.id"
              " where fk.id is null"
              "  and t.varid=?1"
              "  and t.name not like '{%') ut" // Unreferenced tables
              " join tabColumn c"
              " on c.tabid=ut.tabid"
              " and c.autoinc=1"
              " order by 1",
              SUB_EACH,
              0,
              0.5,
              999,
              -1}
             };

static void grading_info(GRADING_T *g) {
    // For debugging
    if (g) {
       printf("--------------\n");
       printf(" code      = %s (%d)\n",
              grad_keyword(g->check_code),
              g->check_code);
       if (g->description) {
         printf(" %s\n", g->description);
       }
       printf(" %-50.50s...\n", g->sql);
       printf(" %-50.50s...\n", g->sql2);
       printf(" rule      = %hd\n", g->rule);
       printf(" threshold = %f\n", g->threshold);
       printf(" val       = %f\n", g->val);
       printf(" cap       = %hd\n", g->cap);
       printf(" next      = %hd\n", g->next);
       printf("--------------\n");
       fflush(stdout);
    }
}

static short read_rules(int gcode, char *str, short *prule,
                        float *pval, short *pcap, int linenum) {
    short  ret = -1;
    char  *p = str;
    char  *q;

    if (str && prule && pval) {
      switch(*p) {
        case '\0':
             *prule = NO_RULE;
             *pval = 0;
             ret = 0;
             break;
        case '+':
        case '-':
        case '*':
             *prule = (*p == '+' ? ADD_ONCE :
                            (*p == '-' ? SUB_ONCE : MULT_ONCE));
             if (*(p+1) == *p) {
               *prule = (2 * (*prule));
               p++;
             }
             p++;
             if (sscanf(p, "%f", pval) != 1) {
               fprintf(stderr,
                       "Missing value in configuration file line %d\n",
                       linenum);
             } else {
               ret = 0;
               // Check if there is a limit
               if ((*prule == ADD_EACH)
                   || (*prule == SUB_EACH)
                   || (*prule == MULT_EACH)) {
                 if ((q = strchr(p, '|')) != NULL) {
                   q++;
                   if (sscanf(q, "%hd", pcap) != 1) {
                     fprintf(stderr,
                         "Invalid limit \"%s\" in configuration file line %d\n",
                         q,
                         linenum);
                     ret = -1;
                     *pcap = (short)999;
                   }
                 } else {
                   *pcap = (short)999;
                 }
               }
             }
             break;
       default:
             while (isspace(*p)) {
               p++;
             }
             if (*p == '\0') {
               *prule = NO_RULE;
               *pval = 0;
               ret = 0;
             } else {
               if (gcode == GRAD_START_GRADE) {
                 *prule = NO_RULE;
                 if (sscanf(p, "%f", pval) != 1) {
                   fprintf(stderr,
                           "Missing value in configuration file line %d\n",
                           linenum);
                 } else {
                   ret = 0;
                 }
               } else {
                 fprintf(stderr,
                         "Invalid rule \"%s\" in configuration file line %d\n",
                         p,
                         linenum);
               }
             }
             break;
     }
   }
   return ret;
}

static int find_index(int check_code) {
    int i = 0;
    int grading_options = sizeof(G_grading)/sizeof(GRADING_T);

    while ((i < grading_options)
           && (G_grading[i].check_code != check_code)) {
       i++;
    }
    return (i < grading_options ? i : -1);
}

extern void read_grading(char *grading_file) {
    FILE *fp;
    char  line[LINE_LEN];
    short linenum = 0;
    char *p;
    char *q;
    int   gcode;
    int   gclosest = GRAD_NOT_FOUND;
    int   smallest_lev;
    int   lev;
    int   i;
    int   current;
    int   next;
    char  ok = 1;
    int   grading_options;
    short previous_check = -1;
    short rule;

    // First finish setting up the default values
    if (G_grading_seq[0] != GRAD_START_GRADE) {
      fprintf(stderr, "Incorrectly set default grading scheme "
                      "(first code must be START_GRADE)\n");
      exit(1);
    }
    grading_options = sizeof(G_grading)/sizeof(GRADING_T);
    // First initialize all starts, and spot the beginning
    // of the chain (START_GRADE)
    for (i = 1; i < grading_options; i++) {
      G_grading[i].next = -1;
      if ((G_chain_start == -1) 
           && (G_grading[i].check_code == GRAD_START_GRADE)) {
        G_chain_start = i;
      }
    }
    if (debugging()) {
      fprintf(stderr, "Chain start: %d\n", G_chain_start);
    }
    // Set the default sequence for check operations
    current = G_chain_start;
    for (i = 1; i < GRAD_COUNT; i++) {
      next = find_index(G_grading_seq[i]);
      if (next == -1) {
        fprintf(stderr,
                "Incorrectly set default grading scheme (%s not found)\n",
                grad_keyword((int)G_grading_seq[i]));
        exit(1);
      }
      G_grading[current].next = next;
      current = next;
    }
    //
    // OK, now look for a configuration file
    //
    if (grading_file) {
      if ((fp = fopen(grading_file, "r")) == NULL) {
        perror(grading_file);
        ok = 0;
      }
    } else {
      fp = fopen("grading.conf", "r");
    }
    if (fp) {
      previous_check = -1;
      if (debugging()) {
        fprintf(stderr, "Reading configuration file\n");
        fprintf(stderr, "previous_check: %d\n", previous_check);
      }
      while (ok && fgets(line, LINE_LEN, fp)) {
        linenum++;
        if ((p = strchr(line, '#')) != NULL) {
          *p = '\0';
        }
        _trim(line);
        p = line;
        if (*p) {
          _skip_spaces(p);
          if ((q = strchr(p, '=')) != NULL) {
            *q++ = '\0';
            _skip_spaces(q);
          }
          _trim(p);
          gcode = grad_search(p);
          if (gcode == GRAD_NOT_FOUND) {
            fprintf(stderr, "Invalid grading control \"%s\"\n", p);
            // Try to find the closest one
            smallest_lev = strlen(p) + 1;
            for (i = 0; i < GRAD_COUNT; i++) {
              lev = levenshtein(p, grad_keyword(i));
              if (lev < smallest_lev) {
                smallest_lev = lev;
                gclosest = i;
              }
            }
            if (gclosest == GRAD_NOT_FOUND) {
              fprintf(stderr, "Do you mean \"%s\"?\n",
                              grad_keyword(gclosest));
            }
            ok = 0;
          } else {
            if (debugging()) {
              fprintf(stderr, "%s (follows %s",
                      p,
                      (previous_check == -1) ?
                      "nothing)\n" :
                      grad_keyword(G_grading[previous_check].check_code));
            }
            if ((previous_check == -1) 
               && (gcode != GRAD_START_GRADE)) {
              // Invalid - Start grade MUST come first
              fprintf(stderr, "start_grade must be specified first"
                              " even when no grading is applied.\n");
              ok = 0;
            } else {
              if (previous_check == -1) {
                previous_check = G_chain_start;
              }
            }
            if (ok && (gcode != GRAD_START_GRADE)) {
              if (debugging()) {
                fprintf(stderr, "[%hd])\n", previous_check);
              }
              i = 0;
              while ((i < grading_options)
                     && ((int)G_grading[i].check_code != gcode)) {
                i++;
              }
              if (i < grading_options) {
                if (debugging()) {
                  fprintf(stderr, "\t%s found at %d\n",
                                  grad_keyword(gcode), i);
                }
                G_grading[previous_check].next = (short)i;
                G_grading[i].next = -1;
                previous_check = (short)i;
              } else {
                if (debugging()) {
                  fprintf(stderr, "\tCouldn't find %s\n", grad_keyword(gcode));
                }
              }
            }
            // Now check what we have on the line
            if (q) {
              switch (*q) {
                case '<':
                case '>':
                     if (strncmp(grad_keyword(gcode), "percent_", 8)
                         && strncmp(grad_keyword(gcode), "number_", 7)) {
                       fprintf(stderr,
                            "Threshold value only valid with"
                            " queries that return a percentage"
                            " or a number, line %d\n",
                            linenum);
                       ok = 0;
                     } else { 
                       rule = (*q == '>' ? AT_MOST : AT_LEAST);
                       q++;
                       // Read the threshold value
                       if (sscanf(q, "[%f]",
                                  &(G_grading[i].threshold)) == 0) { 
                         fprintf(stderr,
                                 "Invalid threshold syntax line %d "
                                 " (%c[threshold value] expected)\n",
                                 (rule == AT_MOST ? '<' : '>'),
                                 linenum);
                         ok = 0; 
                       }
                       if ((q = strchr(q, ']')) != NULL) {
                         q++;
                       }
                     }
                     if (ok) {
                       ok = (0 ==  read_rules(gcode, q, &(G_grading[i].rule),
                                              &(G_grading[i].val),
                                              &(G_grading[i].cap),
                                              linenum));
                     }
                     if (ok) {
                       G_grading[i].rule |= rule;
                     }
                     break;
                default :
                     if ((strncmp(grad_keyword(gcode), "percent_", 8) == 0) 
                        || (strncmp(grad_keyword(gcode), "number_", 7) == 0)) {
                       fprintf(stderr,
                                "Threshold (>[value] or <[value])"
                                 " expected with %s line %d\n",
                                 grad_keyword(gcode), linenum);
                       ok = 0;
                     } else {
                       ok = (0 ==  read_rules(gcode, q,
                                              &(G_grading[i].rule),
                                              &(G_grading[i].val),
                                              &(G_grading[i].cap),
                                              linenum));
                     }
                     break;
              }
            }
          }
        }
      }
      fclose(fp);
      if (debugging()) {
        fprintf(stderr, "From configuration file:\n");
        show_grading();
      }
    } else {
      fprintf(stderr, "Warning: applying the default grading scheme\n");
    }
}

extern void show_grading(void) {
    short i = G_chain_start;
    short rule;
    char  cap[30];
    if (debugging()) {
      fprintf(stderr, "--- G_chain_start: %hd\n", G_chain_start);
    }
    printf("#\n");
    printf("# Rules are expressed as :\n");
    printf("#   rule_name [ = formula]\n");
    printf("# If the formula is absent, the rule will be checked and\n");
    printf("# problems reported, but the rule will not intervene in\n");
    printf("# the computation of a grade.\n");
    printf("# For the start grade (must come first if you grade) the\n");
    printf("# formula is a simple assignment. Otherwise, the formula is\n");
    printf("# an operator (+,-,*) followed by the value applied to\n");
    printf("# the grade (no division but the value isn't necessarily\n");
    printf("# an integer value). If the operator is repeated, the\n");
    printf("# operation is applied for every occurrence. In that case\n");
    printf("# a limit can be set to the maximum number of times the\n");
    printf("# operation is applied with a vertical bar followed by the\n");
    printf("# limit. For instance:\n");
    printf("#     <rule_name> = --5|3\n");
    printf("# will remove 5 points for each violation of the rule, up\n");
    printf("# to 15 points (3 times).\n");
    printf("# Rules the name of which starts with \"percent\" or \"number\"\n");
    printf("# take a comparator (< or >) followed by a threshold value\n");
    printf("# between square brackets before the formula proper.\n");
    printf("# \n");

    while (i != -1) {
      rule = G_grading[i].rule & ~AT_MOST & ~AT_LEAST;
      if (G_grading[i].description) {
        printf("# %s\n", G_grading[i].description);
      }
      if (G_grading[i].rule & AT_LEAST) {
        printf("# when value is smaller than threshold\n");
      } else if (G_grading[i].rule & AT_MOST) {
        printf("# when value is greater than threshold\n");
      }
      sprintf(cap, " up to %hd times", G_grading[i].cap);
      switch(rule) {
        case MULT_EACH:
             printf("# multiply grade by %.1f for each occurrence%s\n",
                     G_grading[i].val,
                     (G_grading[i].cap == 999 ? "": cap));
             break;
        case MULT_ONCE:
             printf("# multiply grade by %.1f%s\n",
                     G_grading[i].val,
                     ((G_grading[i].rule & AT_LEAST)
                      || (G_grading[i].rule & AT_LEAST)) ? 
                     "" : " if it happens");
             break;
        case SUB_EACH:
             printf("# subtract %.1f from grade for each occurrence%s\n",
                    G_grading[i].val,
                    (G_grading[i].cap == 999 ? "": cap));
             break;
        case SUB_ONCE:
             printf("# subtract %.1f from grade%s\n",
                    G_grading[i].val,
                     ((G_grading[i].rule & AT_LEAST)
                      || (G_grading[i].rule & AT_LEAST)) ? 
                     "" : " if it happens");
             break;
        case ADD_EACH:
             printf("# add %.1f to grade for each occurrence%s\n",
                    G_grading[i].val,
                    (G_grading[i].cap == 999 ? "": cap));
             break;
        case ADD_ONCE:
             printf("# add %.1f to grade%s\n",
                     G_grading[i].val,
                     ((G_grading[i].rule & AT_LEAST)
                      || (G_grading[i].rule & AT_LEAST)) ? 
                     "" : " if it happens");
             break;
        default:
             break;
      } 
      printf("%s%s",
             grad_keyword(G_grading[i].check_code),
             (rule == NO_RULE ? "" : " = "));

      if (G_grading[i].rule & AT_MOST) {
        printf(">[%.1f]", G_grading[i].threshold);
      } else if (G_grading[i].rule & AT_LEAST) {
        printf("<[%.1f]", G_grading[i].threshold);
      }
      if (G_grading[i].val && (rule != NO_RULE)) {
        switch(rule) {
            case ASSIGN:
                 break;
            case MULT_EACH:
                 putchar('*');
            case MULT_ONCE:
                 putchar('*');
                 break;
            case SUB_EACH:
                 putchar('-');
            case SUB_ONCE:
                 putchar('-');
                 break;
            case ADD_EACH:
                 putchar('+');
            case ADD_ONCE:
                 putchar('+');
                 break;
            default:
                 break;
        } 
        printf("%.1f", G_grading[i].val);
        switch(rule) {
          case MULT_EACH:
          case SUB_EACH:
          case ADD_EACH:
               if (G_grading[i].cap != 999) {
                 printf("|%hd", G_grading[i].cap);
               }
               break;
          default:
               break;
        }
      }
      putchar('\n');
      i = G_grading[i].next;
      if (debugging()) {
        fprintf(stderr, "--- i: %hd\n", i);
      }
    }
    fflush(stdout);
}

extern void set_model_weight(short val) {
   if ((val >= 0) && (val <= 100)) {
     G_model_weight = val;
   }
}

extern int grade(char report, short refvar, float max_grade, char all_tables) {
    short       i = G_chain_start;
    short       rule;
    float       prev_grade = 0;
    float       work_grade = 0;
    float       model_closeness_grade = 0;
    int         query_result;
    short       k;
    short       rule_cnt = 0;
    char        no_grading = 0;
    OVERVIEW_T  overview;
    short       checks[GRAD_COUNT];

    // Get the start grade
    if (max_grade <= 0) {
      if (i < 0) {
        i = 0;
        no_grading = 1;
      }
      if (G_grading[i].check_code != GRAD_START_GRADE) {
        fprintf(stderr, "Unknown starting grade\n");
        if (debugging()) {
          printf("i = %d (chain start = %d)\n", i, G_chain_start);
          grading_info(&(G_grading[i]));
        } 
        no_grading = 1;
      } else {
        work_grade = G_grading[i].val;
      }
    } else {
      work_grade = max_grade;
    }
    if (!all_tables) {
      only_figures();
    }
    (void)memset(&overview, 0, sizeof(OVERVIEW_T));
    if (refvar <= 0) {
      G_model_weight = 0;
      // Not required if there is a reference model
      // Get overview info
      (void)db_basic_info(&(overview.tabcnt), &(overview.datacnt));
      overview.rel = db_relationships();
      overview.selfref = db_selfref();
      if (report) {
        RELATIONSHIP_T *r;
        NAME_ITEM_T    *n;
        printf("%d tables in the schema and %d pieces of information.\n",
               overview.tabcnt, overview.datacnt);
        if ((r = overview.rel) != NULL) {
          printf("Relationships:\n");
          do { 
             printf("\t%s\t[%s]\t%s\n", r->tab1, r->cardinality, r->tab2);
          } while ((r = r->next) != NULL);
        } else {
          printf("-- No relationships\n");
        }
        if ((n = overview.selfref) != NULL) {
          printf("Tables with self references:\n");
          do { 
            printf("\t%s\n", n->name);
          } while ((n = n->next) != NULL);
        } else {
          printf("-- No self-referencing tables\n");
        }
      }
    } else {
      // Start from the distance between the model and the submission
      // Affects a user-defined portion of the grade (70% by default) 
      model_closeness_grade = schema_matching(refvar);
      if (report) {
        printf("Closeness to an expected model: %.0f%%\n",
               model_closeness_grade);
      }
    }
    prev_grade = work_grade;
    if (report) {
      printf("Starting grade before controls: %.0f%%\n", work_grade);
    }
    if (refvar > 0) {
      // We'll only check what the variant references passes
      get_variant_tests(refvar, checks, GRAD_COUNT);
    } else {
      int k;
      for (k = 0; k < GRAD_COUNT; k++) {
        checks[k] = 1;
      }
    }
    while (i != -1) {
      if (debugging()) { 
        printf("i = %hd\n", i);
        grading_info(&(G_grading[i]));
        fflush(stdout);
      }
      rule = G_grading[i].rule & ~AT_MOST & ~AT_LEAST;
      if (!no_grading && (rule != NO_RULE)) {
        rule_cnt++;
      }
      if (G_grading[i].sql && checks[G_grading[i].check_code]) {
        if (report && G_grading[i].description) {
          printf("%s\n", G_grading[i].description);
        }
        // Depending on the query, the result will either
        // be a percentage or a number of elements found
        query_result = db_runcheck(G_grading[i].check_code,
                                   report,
                                   G_grading[i].sql,
                                   refvar);
        if (G_grading[i].rule & AT_MOST) {
          query_result = (query_result > G_grading[i].threshold ? 1 : 0);
          if (report) {
            if (query_result) {
              printf("\tgreater than limit %d\n", (int)G_grading[i].threshold);
            } else {
              printf("\t*** OK (<= %d)\n", (int)G_grading[i].threshold);
            }
          }
        } else if (G_grading[i].rule & AT_LEAST) {
          query_result = (query_result < G_grading[i].threshold ? 1 : 0);
          if (report) {
            if (query_result) {
              printf("\tlesser than limit %d\n", (int)G_grading[i].threshold);
            } else {
              printf("\t*** OK (>= %d)\n", (int)G_grading[i].threshold);
            }
          }
        }
        if (G_grading[i].val) {
          switch(rule) {
            case MULT_EACH:
                 for (k = 0; k < query_result; k++) {
                   if (k < G_grading[i].cap) {
                     work_grade *= G_grading[i].val;
                   }
                 }
                 if (report && !no_grading && (prev_grade != work_grade)) {
                   printf("\tgrade: %d -> %d\n", (int)(prev_grade+0.5),
                                                 (int)(work_grade+0.5));
                 }
                 break;
            case MULT_ONCE:
                 if (query_result) {
                   work_grade *= G_grading[i].val;
                 }
                 if (report && !no_grading && (prev_grade != work_grade)) {
                   printf("\tgrade: %d -> %d\n", (int)(prev_grade+0.5),
                                                 (int)(work_grade+0.5));
                 }
                 break;
            case SUB_EACH:
                 work_grade -= (_min(query_result, G_grading[i].cap)
                                * G_grading[i].val);
                 if (report && !no_grading && (prev_grade != work_grade)) {
                   printf("\tgrade: %d -> %d\n", (int)(prev_grade+0.5),
                                                 (int)(work_grade+0.5));
                 }
                 break;
            case SUB_ONCE:
                 if (query_result) {
                   work_grade -= G_grading[i].val;
                 }
                 if (report && !no_grading && (prev_grade != work_grade)) {
                   printf("\tgrade: %d -> %d\n", (int)(prev_grade+0.5),
                                                 (int)(work_grade+0.5));
                 }
                 break;
            case ADD_EACH:
                 work_grade += (_min(query_result, G_grading[i].cap)
                                * G_grading[i].val);
                 if (report && !no_grading && (prev_grade != work_grade)) {
                   printf("\tgrade: %d -> %d\n", (int)(prev_grade+0.5),
                                                 (int)(work_grade+0.5));
                 }
                 break;
            case ADD_ONCE:
                 if (query_result) {
                   work_grade += G_grading[i].val;
                 }
                 if (report && !no_grading && (prev_grade != work_grade)) {
                   printf("\tgrade: %d -> %d\n", (int)(prev_grade+0.5),
                                                 (int)(work_grade+0.5));
                 }
                 break;
            default:
                 break;
          } 
        } 
        fflush(stdout);
      } else {
        if (debugging()) {
          if (G_grading[i].sql) {
            fprintf(stderr, "=> %s\n", G_grading[i].description);
            fprintf(stderr, "Check failed by reference model - unchecked\n");
          } 
        }
      }
      i = G_grading[i].next;
      prev_grade = work_grade;
    }
    if (overview.rel) {
      free_relationships(&(overview.rel));
    }
    if (overview.selfref) {
      free_names(&(overview.selfref));
    }
    if (rule_cnt) {
      if (work_grade > 100) {
        work_grade = 100;
      } else {
        if (work_grade < 0) {
          work_grade = 0;
        }
      }
      if (G_model_weight) {
        if (report) {
          printf("Final grade computed as %hd%% closeness to model (%.0f%%)\n",
                  G_model_weight, model_closeness_grade);
          printf("and %hd%% other controls (%.0f%%).\n",
                  (short)(100 - G_model_weight), (work_grade + 0.5));
        }
        work_grade *= (1 - (float)G_model_weight/100.0);
        work_grade += ((G_model_weight/100.0) * model_closeness_grade);
      }
      return (int)(work_grade + 0.5);
    }
    return -1;
}

extern void graderef(short refvar) {
    short       i = G_chain_start;
    short       rule;
    int         query_result;
    char        pass;

    while (i != -1) {
      if (debugging()) { 
        printf("REF i = %hd\n", i);
        grading_info(&(G_grading[i]));
      }
      rule = G_grading[i].rule & ~AT_MOST & ~AT_LEAST;
      if (G_grading[i].sql2) {
        query_result = db_refcheck(G_grading[i].check_code,
                                   G_grading[i].sql2,
                                   refvar);
        if (G_grading[i].rule & AT_MOST) {
          query_result = (query_result > G_grading[i].threshold ? 1 : 0);
          if (query_result) { // Reference fails the test
            pass = 0;
          } else {
            pass = 1;
          }
        } else if (G_grading[i].rule & AT_LEAST) {
          query_result = (query_result < G_grading[i].threshold ? 1 : 0);
          if (query_result) { // Reference fails the test
            pass = 0;
          } else {
            pass = 1;
          }
        } else {
          pass = (query_result == 0);
        }
      }
      (void)insert_variant_test(refvar, (int)G_grading[i].check_code, pass);
      i = G_grading[i].next;
    }
}
