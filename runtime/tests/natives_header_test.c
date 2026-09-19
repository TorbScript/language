/*
 * natives_header_test.c - the check BACKEND 3.7 asks for, on the C side.
 *
 * `runtime/include/torb_natives.h` is generated from the natives manifest (`compiler/src/backend/c/natives.trb`, by
 * `torb natives --header`) and declares every `.Runtime` target that is `.Ready`. Including it *after* `torb.h` makes
 * the C compiler compare the two declarations of every one of those symbols, so a drift between the manifest and the
 * runtime is a compile error and never a link error with a mangled name in it.
 *
 * The other direction - every `native` declaration of `std/` has an entry, and the file on disk is what the manifest
 * renders - is `compiler/tests/natives.test.trb`.
 */

#include "torb.h"

#include "torb_natives.h"

/* Nothing to run: the point of this translation unit is that it compiles. */
extern int torb_natives_header_checked;
int torb_natives_header_checked = 1;
