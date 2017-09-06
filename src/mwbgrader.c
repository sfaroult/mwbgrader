/* =================================================================== *
 *
 *         mwbgrader.c
 *
 *        Written by Stéphane Faroult
 *
 * =================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "miniz.h"
#include "scanxml.h"
#include "dbop.h"
#include "grading.h"
#include "debug.h"

#define OPTIONS        "gc:drm:w:"
#define GRADING_FILE   "grading.conf"
#define MAX_VARIANTS   50

static char G_report = 0;

static void usage(char *prog) {
    fprintf(stderr, "Usage: %s [flags] <.mwb file> [ ... ]\n", prog);
    fprintf(stderr, "  Flags:\n");
    fprintf(stderr,
            " -g             : show applied grading (can be used to\n");
    fprintf(stderr,
            "                  generate a file to override defaults)\n");
    fprintf(stderr,
            " -c <fname>     : use grading configuration found in <fname>\n");
    fprintf(stderr,
            "                  instead of using grading.conf if it exists.\n");
    fprintf(stderr,
            " -m <model[:g]> : reference model provided by the instructor,\n");
    fprintf(stderr,
            "                  optionally followed by the maximum grade (100\n");
    fprintf(stderr,
            "                  by default) if the student submission matches\n");
    fprintf(stderr,
            "                  this model. Option -m can be repeated, or a comma\n");
    fprintf(stderr,
            "                  separated list of models can follow it to allow\n");
    fprintf(stderr,
            "                  for several variants.\n");
    fprintf(stderr,
            " -r             : report on issues found and explain grading.\n");
    fprintf(stderr,
            " -w             : weight given to proximity with the model in\n");
    fprintf(stderr,
            "                  the final grade (default: %d%%).\n",
            MODEL_WEIGHT);
}

int main(int argc, char **argv) {
    char            ch;
    int             i;
    char           *prog = argv[0];
    mz_zip_archive  zip_archive;
    mz_bool         status;
    size_t          uncomp_size;
    char           *p;
    char           *p2;
    char           *q;
    char           *model;
    int             ret;
    int             mwbgrade;
    char           *grading_file = NULL;
    char            display_grading = 0;
    char            refmodel[FILENAME_MAX];
    short           variant;
    short           model_weight = 0;
    char            reference = 0;
    short           variant_arr[MAX_VARIANTS];
    short           variant_cnt = 0;
    
    refmodel[0] = '\0';
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
          case 'm':
               if (refmodel[0] == '\0') {
                 strncpy(refmodel, "refmodel_XXXXXXX", FILENAME_MAX);
                 if ((p =  mktemp(refmodel)) == NULL) {
                   perror("mktemp");
                   exit(1);
                 }
                 // Initialize the model reference
                 db_refconnect(refmodel);
                 reference = 1;
               }
               if ((model = strdup(optarg)) != NULL) {
                 p = model;
                 do {
                   q = strchr(p, ',');
                   if (q) {
                     *q++ = '\0';
                   }
                   // p points to the name of a model optionally
                   // followed by a maximum grade. The maximum
                   // grade part is gone after the call to
                   // insert_variant().
                   if ((variant = insert_variant(p)) == -1) {
                     db_disconnect();
                     unlink(refmodel);
                     exit(1);
                   }
                   if (variant_cnt < MAX_VARIANTS) {
                     variant_arr[variant_cnt++] = variant;
                   }
                   memset(&zip_archive, 0, sizeof(zip_archive));
                   status = mz_zip_reader_init_file(&zip_archive,
                                                    (const char *)p, 0);
                   if (!status) {
                     fprintf(stderr, "Failed to open \"%s\"\n", p);
                     db_disconnect();
                     unlink(refmodel);
                     exit(1);
                   }
                   // We are only interested in "document.mwb.xml"
                   p2 = mz_zip_reader_extract_file_to_heap(&zip_archive,
                                               "document.mwb.xml",
                                               &uncomp_size, 0);
                   if (!p2) {
                     fprintf(stderr, "Couldn't recognize %s as a .mwb file\n",
                                     p);
                     db_disconnect();
                     unlink(refmodel);
                     exit(1);
                   }
                   (void)parseXML(variant, p2, debugging(), debugging());
                   free(p2);
                   mz_zip_reader_end(&zip_archive);
                   if (q) {
                     p = q;
                   }
                 } while (q);
                 free(model);
               }
               break;
          case 'w':
               if (sscanf(optarg, "%hd", &model_weight) == 1) {
                 if ((model_weight < 0) || (model_weight > 100)) {
                   fprintf(stderr,
                    "Model weight must be between 0 and 100 inclusive.\n");
                   exit(1);
                 }
                 set_model_weight(model_weight);
               } else {
                 fprintf(stderr,
           "Model weight must be an integer between 0 and 100 inclusive.\n");
                 exit(1);
               }
               break;
          case '?':
          default:
               usage(prog);
               return 1;
       }
    }
    argc -= optind;
    argv += optind;
    read_grading(grading_file);
    if (display_grading) {
      show_grading();
      if (reference) {
        db_disconnect();
      }
      return 0;
    }
    if (argc == 0) {
      usage(prog);
      if (reference) {
        db_disconnect();
      }
      return 1;
    }
    if ((model_weight != MODEL_WEIGHT) && !reference) {
      fprintf(stderr,
         "Warning: a weight was provided for an absent instructor model.\n");
    }
    // Before anything else, check variants then disconnect from
    // the reference database, if there is one
    if (reference) {
      for (i = 0; i < variant_cnt; i++) {
        graderef(variant_arr[i]);
      }
      // We disconnect from the reference database.
      // It will be later attached to the in-memory
      // database where the submitted model will
      // be uploaded.
      db_disconnect();
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
          short varid = 0;
          int   base_grade = -1;
          int   dpct = 0; // Percentage of data pieces respective to the model
          int   tpct = 0; // Percentages of tables respective to the model

          // Connect to the memory database (it also attaches
          // to the reference database if there is one)
          ret |= db_connect();
          ret |= parseXML(0, p, debugging(), debugging());
          if (reference) {
            // Find the reference model that best matches
            // the submission
            varid =  best_variant(&base_grade, &dpct, &tpct);
          }
          mwbgrade = grade(G_report, varid, (float)base_grade);
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
    if (refmodel[0] != '\0') {
      if (debugging()) {
        fprintf(stderr, "Reference: %s\n", refmodel);
      } else {
        unlink(refmodel);
      }
    }
    return 0;
}
