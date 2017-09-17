#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "kwsearch.h"
#include "grad.h"

static char *G_grad_words[] = {
    "circular_fk",
    "everything_nullable",
    "isolated_tables",
    "multiple_legs",
    "no_pk",
    "no_uniqueness",
    "number_of_info_pieces",
    "number_of_tables",
    "one_one_relationship",
    "percent_commented_columns",
    "percent_commented_tables",
    "percent_single_col_idx",
    "redundant_indexes",
    "same_datatype_for_all_cols",
    "same_varchar_length",
    "single_col_table",
    "start_grade",
    "too_many_fk_to_same",
    "ultra_wide",
    "useless_autoinc",
    NULL};

extern int grad_search(char *w) {
  return kw_search(GRAD_COUNT, G_grad_words, w, "grad");
}

extern char *grad_keyword(int code) {
  return kw_value(GRAD_COUNT, G_grad_words, code);
}

