#ifndef XML_READER_H
#define XML_READER_H

typedef struct tag {
              int    level;
              char  *name;
              int    id;
              struct tag *next;
             }  TAG_T;

//---   MACROS   ---
#define _under(tagid)         (G_tags_head->next && (G_tags_head->next)->tag_code == tagid)
#define _descendent_of(tagid) (G_tags[tagid] != 0)

#endif
