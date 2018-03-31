#ifndef _CATPATH_HEADER

#define _CATPATH_HEADER

#define CATPATH_NOT_FOUND	-1
#define CATPATH_DIAGRAM	  0
#define CATPATH_DIAGRAM_FOREIGNKEY	  1
#define CATPATH_DIAGRAM_TABLEFIGURE	  2
#define CATPATH_DIAGRAM_TABLEFIGURE_DIAGRAM	  3
#define CATPATH_DIAGRAM_TABLEFIGURE_TABLE	  4
#define CATPATH_TABLE	  5
#define CATPATH_TABLE_COLUMN	  6
#define CATPATH_TABLE_FOREIGNKEY	  7
#define CATPATH_TABLE_FOREIGNKEY_INDEX	  8
#define CATPATH_TABLE_FOREIGNKEY_TABLE	  9
#define CATPATH_TABLE_INDEX	 10
#define CATPATH_TABLE_INDEX_INDEXCOLUMN	 11
#define CATPATH_TABLE_INDEX_INDEXCOLUMN_COLUMN	 12

#define CATPATH_KWCOUNT	13

#ifdef __cplusplus
extern "C" {
#endif
extern int   catpath_search(char *w);
extern char *catpath_keyword(int code);

#ifdef __cplusplus
}
#endif
#endif
