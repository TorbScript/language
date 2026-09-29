# The pages of torbscript-testing

Every page of this skill, with what it answers. Search this file for a word, then open the one page that
answers the question. A page marked planned describes a feature that does not compile yet.

## Contents

- standard-library
- how-to
- tooling

## standard-library

- `standard-library/test.md` - **std/test** (package): test and group, the two functions a .test.trb file calls, with assert doing all of the checking.

## how-to

- `how-to/write-a-test.md` - **Write a test** (how-to): Put a test in a file called *.test.trb, by convention under tests/, group related ones, and let assert show the source and the values instead of writing a matcher.

## tooling

- `tooling/torb-test.md` - **torb test** (tooling): torb test runs every *.test.trb file below the paths it is given - one binary for all of them - and prints ok or FAILED for every test call it sees, or JSON Lines for an editor, of every test or of the ones a filter names.
