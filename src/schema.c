#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "schema.h"

extern void free_colfk(COLFOREIGNKEY_T **colfkp) {
    if (colfkp && *colfkp) {
      free_colfk(&((*colfkp)->next));
      free(*colfkp);
      *colfkp = NULL;
    }
}

extern void add_colfk(COLFOREIGNKEY_T **listp, char *id) {
    if (listp) {
      if (*listp == NULL) {
        *listp = calloc(1, sizeof(COLFOREIGNKEY_T));
        if (*listp) {
          strncpy((*listp)->colid, id, ID_LEN);
        }
      } else {
        add_colfk(&((*listp)->next), id);
      }
    }
}

extern void add_refcolfk(COLFOREIGNKEY_T **listp, char *id) {
    if (listp && *listp) {
      if ((*listp)->refcolid[0] == '\0') {
        strncpy((*listp)->refcolid, id, ID_LEN);
      } else {
        add_refcolfk(&((*listp)->next), id);
      }
    }
}

