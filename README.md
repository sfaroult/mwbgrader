# mwbgrader 
Expecting a huge number of students in my database classes (Chinese University), and willing to let them practice database modelling at a relatively advanced level, I was looking for a way of grading a design without needing to study in detail over one hundred complex E/R diagrams. I came with the idea of letting students submit a MySQL WorkBench .mwb file and having that file graded by a program.
The program looks for common design mistakes, and can be used on professional projects as well (grading is optional). The -r flag summarizes what was found and reports on potential issues.
There is a default grading scheme that can be overriden.
The -g option documents all the points that are checked as well as the rules used for computing the grade. Its output can be used to generate a customized grading scheme (the program tries to read "grading.conf" by default); some of the points checked may also be skipped.

Here is the default grading scheme (output when using the -g option without any customized grading file):

```
#
# Rules are expressed as :
#   rule_name \[ = formula\]
# If the formula is absent, the rule will be checked and
# problems reported, but the rule will not intervene in
# the computation of a grade.
# For the start grade (must come first if you grade) the
# formula is a simple assignment. Otherwise, the formula is
# an operator (+,-,*) followed by the value applied to
# the grade (no division but the value isn't necessarily
# an integer value). If the operator is repeated, the
# operation is applied for every occurrence. In that case
# a limit can be set to the maximum number of times the
# operation is applied with a vertical bar followed by the
# limit. For instance:
#     <rule_name> = --5|3
# will remove 5 points for each violation of the rule, up
# to 15 points (3 times).
# Rules the name of which starts with "percent" or "number"
# take a comparator (< or >) followed by a threshold value
# between square brackets before the formula proper.
# 
# Initial grade from which the final grade is computed
start_grade = 100.0
# Required number of tables in the schema
# when value is smaller than threshold
# subtract 15.0 from grade
number_of_tables = <[4.0]-15.0
# Required number of information pieces in the schema
# when value is smaller than threshold
# subtract 5.0 from grade
number_of_info_pieces = <[10.0]-5.0
# Tables without a primary (or unique) key
# subtract 10.0 from grade for each occurrence
no_pk = --10.0
# No mandatory column other than possibly a system-generated one
# subtract 5.0 from grade for each occurrence
everything_nullable = --5.0
# Tables with no other unique columns than possibly a system-generated id
# subtract 4.0 from grade for each occurrence
no_uniqueness = --4.0
# Tables not involved into any FK relationship
# subtract 3.0 from grade for each occurrence
isolated_tables = --3.0
# Presence of circular foreign keys
# subtract 10.0 from grade if it happens
circular_fk = -10.0
# Three-legged (or more) many-to-many relationships
# subtract 2.5 from grade for each occurrence
multiple_legs = --2.5
# Tables in a one-to-one relationship that doesn't look like inheritance
# subtract 3.0 from grade for each occurrence
one_one_relationship = --3.0
# Tables with more than two FKs to the same table
# subtract 1.5 from grade for each occurrence
too_many_fk_to_same = --1.5
# Tables with a single column
# subtract 3.5 from grade for each occurrence
single_col_table = --3.5
# Tables with a number of columns far above other tables
# subtract 1.0 from grade for each occurrence
ultra_wide = --1.0
# Unreferenced tables with a system-generated row identifier
# subtract 0.5 from grade for each occurrence
useless_autoinc = --0.5
# Tables where all columns look like default varchar columns
# subtract 3.5 from grade if it happens
same_datatype_for_all_cols = -3.5
# All varchars have a default 'just in case' value
# subtract 5.0 from grade if it happens
same_varchar_length = -5.0
# Single column indexes as a percentage
# when value is greater than threshold
# subtract 5.0 from grade if it happens
percent_single_col_idx = >[99.0]-5.0
# Single-column indexes made useless by a multiple column index
# subtract 2.0 from grade for each occurrence
redundant_indexes = --2.0
# Percentage of tables with comments
# when value is smaller than threshold
# subtract 5.0 from grade
percent_commented_tables = <\50.0]-5.0
# Percentage of columns with comments
# when value is smaller than threshold
# subtract 5.0 from grade
percent_commented_columns = <[10.0]-5.0
```

There are two basic ways of using the program:
 - Assessing that a model respects the rules above. It says of course nothing about the suitability of the model for any purpose, but ensures that the foundations are sound. This can be used for assessing the design before coding starts on a project.
 - Comparing the model to one or several models acknowledged to be correct. In that case, the -m flag can be followed by the name of a .mwb file provided by the instructor. It's possible to provide several variants, and to assign a maximum grade to each variant, eg
 ```
    mwbgrader -m best_model.mwb -m acceptable_model.mwb:90 submission.mwb
 ```
In the latter case, the "submission.mwb" model will be compared to the two submitted instructor models, and matched to the closest one. If the closest one happens to be acceptable_model.mwb, the starting grade will be 90 instead of 100 (the default). 
  When one or several models are provided, the final grade is made of two components:
   - a grade that measures the closeness to the matching model (70% of the final grade by default)
   - a grade that measures the respect of rules (30% of the final grade by default), which can be at most the grade associated with the matching model. Note that if the model doesn't respect a rule, the check isn't performed for the submission.

 The weight of the closeness to the matching model in the final grade can be changed with the "-w new_weight" flag.

Many thanks to Michael Tefft, Chris Saxon, Michel Cadot and Peter Robson for additional ideas of points to check and feedback, and to Liu Zijian (Falcon) for the idea of setting a limit on the number of times an operation can be applied.
