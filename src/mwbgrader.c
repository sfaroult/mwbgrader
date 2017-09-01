#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "miniz.h"
#include "scanxml.h"
#include "dbop.h"
#include "grading.h"
#include "debug.h"

#define OPTIONS        "gc:dr"
#define GRADING_FILE   "grading.conf"

static char G_report = 0;

static void usage(char *prog) {
    fprintf(stderr, "Usage: %s [flags] <.mwb file> [ ... ]\n", prog);
    fprintf(stderr, "  Flags:\n");
    fprintf(stderr,
            "   -g         : show applied grading (can be used to generate\n");
    fprintf(stderr,
            "        a file to override defaults)\n");
    fprintf(stderr,
            "   -c <fname> : use grading configuration found in <fname>\n");
    fprintf(stderr,
           "                 instead of using grading.conf if it exists.\n");
    fprintf(stderr,
            "   -r         : report on issues found and explain grading.\n");
}

int main(int argc, char **argv) {
    char            ch;
    int             i;
    char           *prog = argv[0];
    mz_zip_archive  zip_archive;
    mz_bool         status;
    size_t          uncomp_size;
    char           *p;
    int             ret;
    int             mwbgrade;
    char           *grading_file = NULL;
    char            display_grading = 0;
    
    while ((ch = getopt(argc, argv, OPTIONS)) != -1) {
       switch (ch) {
          case 'd':
               debug_on(); 
               break;
          case 'g':
               display_grading = 1;
               break;
          case 'r':
               G_report = 1;
               break;
          case 'c':
               grading_file = optarg;
               break;
          case '?':
          default:
               usage(prog);
               return 1;
       }
    }
    argc -= optind;
    argv += optind;
    if (argc == 0) {
      usage(prog);
      return 1;
    }
    read_grading(grading_file);
    if (display_grading) {
      show_grading();
      return 0;
    }
    for (i = 0; i < argc; i++) {
      memset(&zip_archive, 0, sizeof(zip_archive));
      status = mz_zip_reader_init_file(&zip_archive, (const char *)argv[i], 0);
      if (!status) {
        fprintf(stderr, "Failed to open \"%s\"\n", argv[i]);
        ret = 1;
      } else {
        // We are only interested in "document.mwb.xml"
        p = mz_zip_reader_extract_file_to_heap(&zip_archive,
                                               "document.mwb.xml",
                                               &uncomp_size, 0);
        if (!p) {
          fprintf(stderr, "Couldn't recognize %s as a .mwb file\n",
                          argv[i]);
          ret = 1;
        } else {
          // Connect to the memory database
          ret |= db_connect();
          ret |= parseXML(p, debugging(), debugging());
          mwbgrade = grade(G_report);
          if (mwbgrade >= 0) {
            if (G_report) {
              printf("grade = %d\n", mwbgrade);
            } else {
              printf("%d\n", mwbgrade);
            }
          }
          // Release the memory database
          db_disconnect();
          free(p);
        } 
        mz_zip_reader_end(&zip_archive);
      }
    }
    return 0;
}
