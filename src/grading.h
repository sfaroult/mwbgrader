#ifndef _GRADING_H

#define _GRADING_H

#define MODEL_WEIGHT   70

extern void read_grading(char *grading_file);
extern void show_grading(void);
extern int  grade(char report, short refvar, float max_grade, char all_tables);
extern void graderef(short refvar);
extern void set_model_weight(short val);

#endif
