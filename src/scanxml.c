#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <libxml/xmlreader.h>
#ifdef SETUP
#include <ctype.h>
#include <assert.h>
#endif

#include "xml_reader.h"
#include "xmlnodes.h"
#include "schema.h"
#include "dbop.h"
#include "grading.h"
#ifndef SETUP
#include "mwbkey.h"
#include "synth.h"
#include "catpath.h"
#endif

#ifdef STANDALONE
#define OPTIONS    "d"
#endif

#define MAXLEVELS      100
#define SYNTH_ID_LEN   500

#define STATUS_CNT   5
#define STATUS_COL   0
#define STATUS_FK    1
#define STATUS_IDX   2
#define STATUS_ICO   3
#define STATUS_TAB   4
#define STATUS_UNK  99

#define _short(s)   (s?(strrchr(s,'.') == NULL ?s:1+strrchr(s,'.')):"null")

typedef struct {
#ifdef SETUP
                char   catpath[SYNTH_ID_LEN];
#else
                short  catpath;
#endif
                short  cat;
                int    key;
                char   obj;
               } STATUS_T;

static TAG_T    *G_tags_head = (TAG_T *)NULL;
static char      G_debug = 0;
static int       G_id = 1;
static STATUS_T  G_status[MAXLEVELS];
static short     G_lvl = 0;

// content-struct-name (for type = list) and struct-name
// For struct-name there is an id
static char     *G_category[STATUS_CNT] = {"Column",
                                           "ForeignKey",
                                           "Index",
                                           "IndexColumn",
                                           "Table"};

/* Structures holding current values */
static TABTABLE_T      G_tab;
static TABCOLUMN_T     G_col;
static TABINDEX_T      G_idx;
static TABINDEXCOL_T   G_idxcol;
static TABFOREIGNKEY_T G_fk;

// Debugging
static void show_tag(char exiting, char *tag) {
   int cat = G_status[G_lvl].cat;
   if (cat >= 0
       && cat < STATUS_CNT) {
      if (exiting) {
        fprintf(stderr, "</%s>", tag);
      } else {
        fprintf(stderr, "\n%*.*s<%s>",
           G_lvl, G_lvl, " ",
           tag);
      }
   }
}

static void init_struct(short variant) {
    memset(&G_tab, 0, sizeof(TABTABLE_T));
    G_tab.varid = variant;
    memset(&G_col, 0, sizeof(TABCOLUMN_T));
    G_col.varid = variant;
    memset(&G_idx, 0, sizeof(TABINDEX_T));
    G_idx.varid = variant;
    memset(&G_idxcol, 0, sizeof(TABINDEXCOL_T));
    G_idxcol.varid = variant;
    memset(&G_fk, 0, sizeof(TABFOREIGNKEY_T));
    G_fk.varid = variant;
}

#ifdef LIBXML_READER_ENABLED

#ifdef DEBUG
static void tag_display(TAG_T *t) {
   // We always insert at the head of the list,
   // so we have nodes in reverse order
   if (t) {
      tag_display(t->next);
      printf("%s#%d(%d)%s", t->name, t->id, t->level,
                            (t != G_tags_head ? ">":""));
   } else {
      putchar('\n');
      fflush(stdout);
   }
}
#endif
     
static void tag_insert(TAG_T **tagpp, char *tagname) {
   TAG_T  *t;

   if (tagpp) {
      // if (G_debug) {
      //   fprintf(stderr, "Creating tag node %s ...", tagname);
      // }
      if ((t = (TAG_T *)malloc(sizeof(TAG_T))) != (TAG_T *)NULL) {
         t->level = G_lvl;
         t->name = strdup(tagname);
         t->id = G_id++;
         t->next = *tagpp;
         *tagpp = t;
      } else {
        perror("malloc");
        exit(1);
      }
      // if (G_debug) {
      //   fprintf(stderr, " done\n");
      // }
   }
}

static void tag_remove(TAG_T **tagpp) {
   TAG_T  *t;

   if (tagpp) {
      t = *tagpp;
      if (t) {
         if (t->name) {
            free(t->name);
         }
         *tagpp = t->next;
         free(t);
      }
   }
}

static int checkAttr(xmlTextReaderPtr reader, char *tag) {
    const xmlChar *n = NULL;
    const xmlChar *v = NULL;
          int      i;
          int      cat;
          char     catpath[SYNTH_ID_LEN];
#ifdef SETUP
          FILE    *fpkey;
          FILE    *fpsynth;
          FILE    *fpcatpath;
          char    *p;
#else
          int      keyid;
          char     synth_id[SYNTH_ID_LEN];
#endif

    if (xmlTextReaderHasAttributes(reader)) {
#ifdef SETUP
      fpkey = fopen("mwbkey.txt", "a");
      assert(fpkey);
      fpsynth = fopen("synth.txt", "a");
      assert(fpsynth);
      fpcatpath = fopen("catpath.txt", "a");
      assert(fpcatpath);
#endif
      if (G_lvl) {
        // A priori, same category as the previous level
        G_status[G_lvl].cat = G_status[G_lvl-1].cat;
#ifdef SETUP
        strncpy(G_status[G_lvl].catpath, G_status[G_lvl-1].catpath,
                SYNTH_ID_LEN);
#else
        G_status[G_lvl].catpath = G_status[G_lvl-1].catpath;
#endif
        if (strcasecmp(tag, "link") == 0) {
          int lvl = G_lvl;

          do {
            lvl--;
          } while ((lvl >= 0) && (G_status[lvl].key < 0));
          if (lvl >= 0) {
            G_status[G_lvl].key = G_status[lvl].key;
          }
        } else {
#ifdef SETUP
          G_status[G_lvl].key = -1;
#else
          G_status[G_lvl].key = SYNTH_NOT_FOUND;
#endif
        }
      } else {
        G_status[G_lvl].cat = STATUS_UNK;
#ifdef SETUP
        G_status[G_lvl].catpath[0] = '\0';
        G_status[G_lvl].key = -1;
#else
        G_status[G_lvl].catpath = CATPATH_NOT_FOUND;
        G_status[G_lvl].key = SYNTH_NOT_FOUND;
#endif
      }
      G_status[G_lvl].obj = 0;
      // Now loop on attributes
      while (xmlTextReaderMoveToNextAttribute(reader)) {
        n = xmlTextReaderConstName(reader);
        v = xmlTextReaderConstValue(reader);
        // We are only interested in attributes "type",
        // "struct-name", "id" and "key". We return the compound
        // code for "key", if found (the compound code combines
        // the category and key code)
        if (strcmp((char *)n, "type") == 0) {
          if (strcmp((char *)v, "object") == 0) {
            G_status[G_lvl].obj = 1;
          }
        } else if (strcmp((char *)n, "struct-name") == 0) {
          // Possibly a new category
          i = 0;
          while ((i < STATUS_CNT)
                 && strcmp(_short((char *)v), G_category[i])) {
            i++;
          }
          if ((i < STATUS_CNT)
              && (i != G_status[G_lvl].cat)) { // New category
            catpath[0] = '\0';
            if (G_status[G_lvl].cat != STATUS_UNK) {
#ifdef SETUP
              strncpy(catpath, G_status[G_lvl].catpath, SYNTH_ID_LEN);
#else
              strncpy(catpath, catpath_keyword(G_status[G_lvl].catpath),
                        SYNTH_ID_LEN);
#endif
              strncat(catpath, "|", SYNTH_ID_LEN - 1 - strlen(catpath));
            }
            strncat(catpath, G_category[i],
                    SYNTH_ID_LEN - strlen(catpath) - strlen(G_category[i]));
            G_status[G_lvl].cat = i;
#ifdef SETUP
            strncpy(G_status[G_lvl].catpath, catpath, SYNTH_ID_LEN);
#else
            G_status[G_lvl].catpath = (short)catpath_search(catpath);
#endif
          } else {
            // Not an object for us to consider
            G_status[G_lvl].obj = 0;
          }
        } else if (G_status[G_lvl].obj && (strcmp((char *)n, "id") == 0)) {
          if (G_debug) {
            fprintf(stderr, "\nstatus = %s\n",
                    G_category[G_status[G_lvl].cat]);
          }
          switch (G_status[G_lvl].cat) {
            case STATUS_COL:
                 strncpy(G_col.id, (char *)v, ID_LEN);
                 strncpy(G_col.tabid, G_tab.id, ID_LEN);
                 break;
            case STATUS_FK:
                 strncpy(G_fk.id, (char *)v, ID_LEN);
                 strncpy(G_fk.tabid, G_tab.id, ID_LEN);
                 break;
            case STATUS_IDX:
                 // The index is first inserted, then
                 // updated when everything is over.
                 // All mandatory columns need a value.
                 strncpy(G_idx.id, (char *)v, ID_LEN);
                 strncpy(G_idx.tabid, G_tab.id, ID_LEN);
                 strncpy(G_idx.name, (char *)v, NAME_LEN);
                 break;
            case STATUS_ICO:
                 // Ignore the id. Useless.
                 //strncpy(G_idxcol.id, (char *)v, ID_LEN);
                 strncpy(G_idxcol.idxid, G_idx.id, ID_LEN);
                 break;
            case STATUS_TAB:
                 // The table is first inserted, then
                 // updated when everything is over.
                 // All mandatory columns need a value.
                 strncpy(G_tab.id, (char *)v, ID_LEN);
                 strncpy(G_tab.name, (char *)v, NAME_LEN);
                 break;
            default:
                 break;
          }
        } else if ((strcmp((char *)n, "key") == 0)
                   && (G_status[G_lvl].cat != STATUS_UNK)) {
          cat = G_status[G_lvl].cat;
#ifdef SETUP
          p = G_status[G_lvl].catpath;
          while (*p) {
            fputc(toupper(*p++), fpsynth);
          }
          fputc('|', fpsynth);
          p = (char *)v;
          while (*p) {
            fputc(toupper(*p++), fpsynth);
          }
          fputc('\n', fpsynth);
          p = (char *)v;
          while (*p) {
            fputc(toupper(*p++), fpkey);
          }
          fputc('\n', fpkey);
          p = G_status[G_lvl].catpath;
          while (*p) {
            fputc(toupper(*p++), fpcatpath);
          }
          fputc('\n', fpcatpath);
#else
          if (G_debug) {
            fprintf(stderr, "looking for %s ", (char *)v);
          }
          keyid = mwbkey_search((char *)v);
          switch(keyid) {
            case MWBKEY_NOT_FOUND:
                 if (G_debug) {
                   fprintf(stderr, "NOT FOUND\n");
                 }
                 break;
            default:
                 snprintf(synth_id, SYNTH_ID_LEN,
                          "%s|%s",
                          catpath_keyword(G_status[G_lvl].catpath),
                          (char *)v);
                 if (G_debug) {
                   fprintf(stderr, " - %s ", synth_id);
                 }
                 G_status[G_lvl].key = synth_search(synth_id);
                 if (G_debug) {
                   fprintf(stderr, "(%d)\n", G_status[G_lvl].key);
                 }
                 break;
          }
#endif
        }
      }
      // Move the reader back to the element node.
      xmlTextReaderMoveToElement(reader);
#ifdef SETUP
      fclose(fpkey);
      fclose(fpsynth);
      fclose(fpcatpath);
#endif
    }
    return G_status[G_lvl].key;
}

static int streamXML(xmlTextReaderPtr reader, short variant) {
           int              ret;
           int              type;
           char            *name;
#ifndef SETUP
           int              cat;
     const xmlChar         *val;
           int              getCatKey = SYNTH_NOT_FOUND;
           short            prevCatPath = CATPATH_NOT_FOUND;
           COLFOREIGNKEY_T *fkcol_list = NULL;
           int              seq = 1;
#else
           int              getCatKey = -1;
#endif

     if (reader != NULL) {
        // if (G_debug) {
        //   fprintf(stderr, "Calling xmlTextReaderRead()\n");
        // }
        ret = xmlTextReaderRead(reader);
        // if (G_debug) {
        //   fprintf(stderr, "starting loop\n");
        // }
        while (ret == 1) {
           type = xmlTextReaderNodeType(reader);
           // if (G_debug) {
           //   fprintf(stderr, "Node type = %d\n", type);
           // }
           switch(type) {
              case XMLSTART:
                   // if (G_debug) {
                   //   fprintf(stderr, "XMLSTART\n");
                   // }
                   if (!xmlTextReaderIsEmptyElement(reader)) {
                     name = (char *)xmlTextReaderConstName(reader);
                     G_lvl = (short)xmlTextReaderDepth(reader);
                     tag_insert(&G_tags_head, name);
                     // if (G_debug) {
                     //   fprintf(stderr, "Calling checkAttr()\n");
                     // }
                     getCatKey = checkAttr(reader, name);
                     // if (G_debug) {
                     //   fprintf(stderr, "back from checkAttr()\n");
                     // }
#ifndef SETUP
                     if (G_status[G_lvl].catpath != prevCatPath) {
                       // if (G_debug) {
                       //   fprintf(stderr, "New catpath %s\n",
                       //        catpath_keyword(G_status[G_lvl].catpath));
                       // }
                       prevCatPath = G_status[G_lvl].catpath;
                       switch (G_status[G_lvl].catpath) {
                         case CATPATH_TABLE_INDEX:
                              seq = 1;
                              break;
                         default:
                              break;
                       }
                     }
                     // if (G_debug) {
                     //   show_tag(0, name);
                     // }
#endif
                   }
                   break;
              case XMLEND:
                   // if (G_debug) {
                   //   fprintf(stderr, "XMLEND\n");
                   // }
#ifndef SETUP
                   name = (char *)xmlTextReaderConstName(reader);
                   G_lvl = (short)xmlTextReaderDepth(reader);
                   // if (G_debug) {
                   //   show_tag(1, name);
                   // }
                   if (G_status[G_lvl].obj
                       && (strcasecmp(name, "value") == 0)) {
                     cat = G_status[G_lvl].cat;

                     if ((cat >= 0) && (cat < STATUS_CNT)) {
                       if (G_debug) {
                         fprintf(stderr,
                                 "\n%s - Leaving %s object --> insert\n",
                                 catpath_keyword(G_status[G_lvl].catpath),
                                 G_category[cat]);
                       }
                       switch (cat) {
                         case STATUS_COL:
                              if (G_debug) {
                                fprintf(stderr, "Inserting column:\n");
                                fprintf(stderr, "varid: %hd\n", G_col.varid);
                                fprintf(stderr, "id   : %s\n", G_col.id);
                                fprintf(stderr, "tabid: %s\n", G_col.tabid);
                                fprintf(stderr, "name : %s\n", G_col.name);
                                fprintf(stderr, "type : %s\n", G_col.datatype);
                              }
                              (void)insert_column(&G_col);
                              memset(&G_col, 0, sizeof(TABCOLUMN_T));
                              G_col.varid = variant;
                              break;
                         case STATUS_FK:
                              if (G_debug) {
                                fprintf(stderr, "Inserting FK:\n");
                                fprintf(stderr, "varid: [%hd]\n", G_fk.varid);
                                fprintf(stderr, "id   : [%s]\n", G_fk.id);
                                fprintf(stderr, "name : [%s]\n", G_fk.name);
                                fprintf(stderr, "tabid: [%s]\n",
                                                G_fk.tabid);
                                fprintf(stderr, "reftabid : [%s]\n",
                                                G_fk.reftabid);
                              }
                              (void)insert_foreignkey(&G_fk, fkcol_list);
                              memset(&G_fk, 0, sizeof(TABFOREIGNKEY_T));
                              G_fk.varid = variant;
                              free_colfk(&fkcol_list);
                              break;
                         case STATUS_IDX:
                              if (G_debug) {
                                fprintf(stderr, "Inserting index (1):\n");
                                fprintf(stderr, "id   : %s\n", G_idx.id);
                                fprintf(stderr, "name : %s\n", G_idx.name);
                                fprintf(stderr, "tabid: %s\n", G_idx.tabid);
                              }
                              (void)insert_index(&G_idx);
                              memset(&G_idx, 0, sizeof(TABINDEX_T));
                              G_idx.varid = variant;
                              break;
                         case STATUS_ICO:
                              if (G_debug) {
                                fprintf(stderr, "Inserting index (2):\n");
                                fprintf(stderr, "id   : %s\n", G_idx.id);
                                fprintf(stderr, "name : %s\n", G_idx.name);
                                fprintf(stderr, "tabid: %s\n", G_idx.tabid);
                              }
                              (void)insert_index(&G_idx);
                              // DON'T reinit the structure
                              strncpy(G_idxcol.tabid, G_idx.tabid, ID_LEN);
                              if (G_debug) {
                                fprintf(stderr, "Inserting index column:\n");
                                fprintf(stderr, "tabid: %s\n", G_idxcol.tabid);
                                fprintf(stderr, "index: %s\n", G_idxcol.idxid);
                                fprintf(stderr, "col  : %s\n", G_idxcol.colid);
                              }
                              (void)insert_indexcol(&G_idxcol);
                              memset(&G_idxcol, 0, sizeof(TABINDEXCOL_T));
                              G_idxcol.varid = variant;
                              break;
                         case STATUS_TAB:
                              if (G_debug) {
                                fprintf(stderr, "Inserting table:\n");
                                fprintf(stderr, "varid: %hd\n", G_tab.varid);
                                fprintf(stderr, "id   : %s\n", G_tab.id);
                                fprintf(stderr, "name : %s\n", G_tab.name);
                              }
                              (void)insert_table(&G_tab);
                              memset(&G_tab, 0, sizeof(TABTABLE_T));
                              G_tab.varid = variant;
                              break;
                         default:
                              break;
                       }
                     }
                   }
                   tag_remove(&G_tags_head);
#endif
                   break;
              case XMLTEXT:
                   //if (G_debug) {
                   //  fprintf(stderr, "XMLTEXT\n");
                   //}
#ifndef SETUP
                   if ((G_status[G_lvl].cat != STATUS_UNK)
                       && (getCatKey != SYNTH_NOT_FOUND)) {
                     char *v;
                     cat = G_status[G_lvl].cat;
                     val = xmlTextReaderConstValue(reader);
                     switch (getCatKey) {
                       case SYNTH_TABLE_COLUMN_COMMENT:
                            if (val && *val) {
                              v = (char *)val;
                              while (isspace(*v)) {
                                v++;
                              }
                              G_col.comment_len = strlen(v);
                            }
                            break;
                       case SYNTH_TABLE_COMMENT:
                            if (val && *val) {
                              v = (char *)val;
                              while (isspace(*v)) {
                                v++;
                              }
                              G_tab.comment_len = strlen(v);
                            }
                            break;
                       case SYNTH_TABLE_COLUMN_AUTOINCREMENT:
                            G_col.autoinc = (char)*val;
                            break;
                       case SYNTH_TABLE_COLUMN_DEFAULTVALUE:
                            if (val) {
                              G_col.defaultvalue = strdup((char *)val);
                            }
                            break;
                       case SYNTH_TABLE_COLUMN_ISNOTNULL:
                            G_col.isnotnull = (char)*val;
                            break;
                       case SYNTH_TABLE_COLUMN_LENGTH:
                            (void)sscanf((char *)val,
                                         "%hd",
                                         &G_col.collength);
                            break;
                       case SYNTH_TABLE_COLUMN_NAME:
                            strncpy(G_col.name, (char *)val, NAME_LEN);
                            break;
                       case SYNTH_TABLE_COLUMN_PRECISION:
                            (void)sscanf((char *)val,
                                         "%hd",
                                         &G_col.precision);
                            break;
                       case SYNTH_TABLE_COLUMN_SCALE:
                            (void)sscanf((char *)val,
                                         "%hd",
                                         &G_col.scale);
                            break;
                       case SYNTH_TABLE_COLUMN_SIMPLETYPE:
                            strncpy(G_col.datatype,
                                    _short((char *)val),
                                    TYPE_LEN);
                            break;
                       case SYNTH_TABLE_FOREIGNKEY_NAME:
                            strncpy(G_fk.name, (char *)val, NAME_LEN);
                            if (G_debug) {
                              fprintf(stderr,
                                      "xml - found foreign key name %s\n",
                                      G_fk.name);
                            }
                            break;
                       case SYNTH_TABLE_INDEX_INDEXCOLUMN_COLUMN_REFERENCEDCOLUMN:
                            strncpy(G_idxcol.colid, (char *)val, ID_LEN);
                            G_idxcol.seq = seq++;
                            break;
                       case SYNTH_TABLE_INDEX_INDEXCOLUMN_NAME:
                            strncpy(G_idxcol.idxid, (char *)val, ID_LEN);
                            break;
                       case SYNTH_TABLE_INDEX_ISPRIMARY:
                            G_idx.isprimary = (char)*val;
                            break;
                       case SYNTH_TABLE_INDEX_NAME:
                            strncpy(G_idx.name, (char *)val, NAME_LEN);
                            break;
                       case SYNTH_TABLE_INDEX_PRIMARYKEY:
                            break;
                       case SYNTH_TABLE_INDEX_UNIQUE:
                            G_idx.isunique = (char)*val;
                            break;
                       case SYNTH_TABLE_FOREIGNKEY_COLUMNS:
                            add_colfk(&fkcol_list, (char *)val);
                            if (G_debug) {
                              fprintf(stderr,
                                      "xml - found foreign key column %s\n",
                                       (char *)val);
                            }
                            break;
                       case SYNTH_TABLE_FOREIGNKEY_REFERENCEDCOLUMNS:
                            add_refcolfk(&fkcol_list, (char *)val);
                            if (G_debug) {
                              fprintf(stderr,
                                      "xml - found foreign key ref column %s\n",
                                      (char *)val);
                            }
                            break;
                       case SYNTH_TABLE_INDICES:
                            break;
                       case SYNTH_TABLE_NAME:
                            strncpy(G_tab.name, (char *)val, NAME_LEN);
                            break;
                       case SYNTH_TABLE_FOREIGNKEY_TABLE_REFERENCEDTABLE:
                            strncpy(G_fk.reftabid, (char *)val, ID_LEN);
                            break;
                       default:
                            break;
                     }
                     //if (G_debug) {
                     //  if (synth_keyword(getCatKey)) {
                     //    fprintf(stderr, "%s [%s] %s = %s",
                     //         G_category[cat],
                     //         synth_keyword(getCatKey),
                     //         G_tags_head->name,
                     //         (char *)val);
                     //  } else {
                     //    fprintf(stderr, "%s [%d] %s = %s",
                     //         G_category[cat],
                     //         getCatKey,
                     //         G_tags_head->name,
                     //         (char *)val);
                     //  }
                     //}
                   }
#endif
                   break;
              default:
                   break;
           }
           ret = xmlTextReaderRead(reader);
        }
        xmlFreeTextReader(reader);
        if (ret != 0) {
           fprintf(stderr, "XML : failed to parse\n");
        }
     }
     return 0;
}

static int streamFile(const char *filename) {
     xmlTextReaderPtr reader;
     int              ret = -1;

     if (filename) {
        reader = xmlReaderForFile(filename, NULL, 0);
        if (reader != NULL) {
           ret = streamXML(reader, 0);
        } else {
          fprintf(stderr, "Unable to open %s\n", filename);
        }
     }
     return ret;
}

static int streamFileDescriptor(const int fd) {
     xmlTextReaderPtr reader;
     int              ret = -1;

     if (fd >= 0) {
        reader =  xmlReaderForFd(fd, NULL, NULL, 0);
        if (reader != NULL) {
           ret = streamXML(reader, 0);
        } else {
          fprintf(stderr, "Unable to open file descriptor %d\n", fd);
        }
     }
     return ret;
}

#ifndef STANDALONE
static int streamMemory(short variant, const char *p) {
     xmlTextReaderPtr reader;
     int              ret = -1;

     if (p) {
        reader = xmlReaderForMemory(p, strlen(p), NULL, NULL, 0);  
        if (reader != NULL) {
           ret = streamXML(reader, variant);
        } else {
          fprintf(stderr, "Unable to create XML reader for memory\n");
        }
     }
     return ret;
}
#endif

static void XMLstart(void) {
  /*
   * this initializes the library and checks for potential ABI mismatches
   * between the version it was compiled for and the actual shared
   * library used.
   */
   LIBXML_TEST_VERSION

   // Initialization
}

static void XMLend(void) {
  /*
   * Cleanup function for the XML library.
   */
   xmlCleanupParser();
  /*
   * this is to debug memory for regression tests
   */
   // xmlMemoryDump();
}

#ifdef STANDALONE
int main(int argc, char **argv) {
    int opt;

    while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
      switch (opt) {
         case 'd':
              G_debug = 1;
              break;
         default: /* '?' */
              fprintf(stderr, "Usage: %s [-d] <XML file name>\n",
              argv[0]);
              exit(EXIT_FAILURE);
      }
    }
    if (optind >= argc) {
      fprintf(stderr, "Expected XML file name\n");
      exit(EXIT_FAILURE);
    }
    init_struct(0);
    (void)db_connect();
    XMLstart();
    (void)db_begin_tx();
    (void)streamFile(argv[optind]);
    (void)db_commit();
    XMLend();
    (void)db_disconnect();
    return(0);
}
#else
extern int parseXML(short variant, char *xml, char show_tags, char debug) {
    int ret = -1;

    if (debug) {
      G_debug = 1;
    }
    init_struct(variant);
    XMLstart();
    (void)db_begin_tx();
    ret = streamMemory(variant, (const char *)xml);
    (void)db_commit();
    XMLend();
    return ret;
}

extern int parsePipedXML(int fd, char show_tags, char debug) {
    int ret = -1;
    if (debug) {
      G_debug = 1;
    }
    init_struct(0);
    XMLstart();
    (void)db_begin_tx();
    ret = streamFileDescriptor((const int)fd);
    (void)db_commit();
    XMLend();
    return ret;
}
#endif

#else
#ifdef STANDALONE
     int main(void) {
        fprintf(stderr, "XInclude support not compiled in\n");
        exit(1);
     }
#else
extern void parseXML(short variant, char *xml, char show_tags, char debug) {
        fprintf(stderr, "XInclude support not compiled in\n");
        exit(1);
    }
}

extern void parsePipedXML(int fd, char show_tags, char debug) {
        fprintf(stderr, "XInclude support not compiled in\n");
        exit(1);
    }
}
#endif
#endif
