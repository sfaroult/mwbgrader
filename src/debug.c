/*
 *    Simple generic debugging routines
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static char G_debug = 0;

extern void debug_on(void) {
    G_debug = 1;
}

extern void debug_off(void) { // Totally inhibits debugging
    G_debug = 0;
}

extern char debugging(void) {
    return G_debug;
}

extern void debug(short indent, const char *fmt, ...) {
   int i;
   va_list argp;

   if (G_debug && fmt) {
     va_start(argp, fmt);
     if (indent) {
       for (i = 0; i < indent; i++) {
         fputc(' ', stderr);
       }
     }
     vfprintf(stderr, fmt, argp);
     fputc('\n', stderr);
     fflush(stderr);
   }
}

extern void debug_no_nl(short indent, const char *fmt, ...) {
   int i;
   va_list argp;

   if (G_debug && fmt) {
     va_start(argp, fmt);
     if (indent) {
       for (i = 0; i < indent; i++) {
         fputc(' ', stderr);
       }
     }
     vfprintf(stderr, fmt, argp);
     fflush(stderr);
   }
}

