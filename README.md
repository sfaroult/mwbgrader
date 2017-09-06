# mwbgrader 
Expecting a huge number of students in my database classes (Chinese University), and willing to let them practice database modelling at a relatively advanced level, I was looking for a way of grading a design without needing to study in detail over one hundred complex E/R diagrams. I came with the idea of letting students submit a MySQL WorkBench .mwb file and having that file graded by a program.
The program looks for common design mistakes, and can be used on professional projects as well (grading is optional). The -r flag summarizes what was found and reports on potential issues.
There is a default grading scheme that can be overriden.
The -g option documents all the points that are checked as well as the rules used for computing the grade. Its output can be used to generate a  customized grading scheme; some of the points checked may also be skipped.

There are two basic ways of using the program:
 - Assessing that a model respects a number of rules. It says of course nothing about the suitability of the model for any purpose, but ensures that the foundations are sound. This can be used for assessing the design before coding starts on a project.
 - Comparing the model to one or several models acknowledged to be correct. In that case, the -m flag can be followed by the name of a .mwb file provided by the instructor. It's possible to provide several variants, and to assign a maximum grade to each variant, eg
    mwbgrader -m best_model.mwb -m acceptable_model.mwb:90 submission.mwb
In that case, the "submission.mwb" model will be compared to the two submitted instructor models, and matched to the closest one. If the closest one happens to be acceptable_model.mwb, the starting grade will be 90 instead of 100 (the default). 
  When one or several models are provided, the final grade is made of two components:
   - a grade that measures the closeness to the matching model (70% of the final grade by default)
   - a grade that measures the respect of rules (30% of the final grade by default), which can be at most the grade associated with the matching model. Note that if the model doesn't respect a rule, the check isn't performed for the submission.

 The weight of the closeness to the matching model in the final grade can be changed with the "-w new_weight" flag.

Many thanks to Michael Tefft, Chris Saxon, Michel Cadot and Peter Robson for additional ideas of points to check and feedback.
