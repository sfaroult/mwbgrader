# mwbgrader 
Expecting a huge number of students in the database classes, and willing to let them practice database modelling at a relatively advanced level, I couldn't serxuiously grade 200 E/R diagrams. I came with the idea of letting them submit a MySQL WorkBench .mwb file and having that file graded by a program.
The program looks for common design mistakes, and can be used on professional projects as well (grading is optional). The -r flag summarizes what was found and reports on potential issues.
There is a default grading scheme that can (so far untested) be overriden. The -g option shows all the points checked as well as the rules used for computing the grade. Its output can be used to generate a  customized grading scheme; some of the points checked may also be skipped.

This is work in progress. I plan to add the ability to compare student work with an instructor-supplied .mwb; nevertheless, it's already usable, including for providing some feedback about on-going database design work.

Many thanks to Michael Tefft, Chris Saxon, Michel Cadot and Peter Robson for additional ideas of points to check and feedback.
