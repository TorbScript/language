/*
 * torb_machine.h - the kernel of the bytecode VM, between runtime/machine.c and the table of thunks that
 * `torb natives --header` writes into runtime/machine_natives.c (docs/design/VM.md section 6).
 *
 * Nothing but those two files includes it: the VM reaches the kernel through the six natives of `std/machine`, whose
 * prototypes are in torb_natives.h like every other native's.
 */

#ifndef TORB_MACHINE_H
#define TORB_MACHINE_H

#include "torb.h"

/**
 * One function of the runtime, called with the words of the registers: `words` is the whole register file, `base` the
 * frame's first word, and `operands` the result register (-1 for none), one register per argument, and the index of the
 * location (-1 for none). Borrowed, all of them.
 */
typedef void (*torb_machine_native)(int64_t *words, int64_t base, const int64_t *operands);

/** The number of the first function of the runtime among the operations: `kernelNativeBase` of the bytecode format. */
#define TORB_MACHINE_NATIVE_BASE 1024

/** One thunk per ready runtime function of the manifest, in the manifest's sorted order. Generated. */
extern const torb_machine_native torb_machine_natives[];
extern const size_t torb_machine_native_count;

/** The registers of the list, which is made unique first: the pointer is valid until the list grows. */
int64_t *torb_machine_words(torb_list *words);

/** Where a reference word points: an odd word is a register (`index * 2 + 1`), an even one an address. */
void *torb_machine_address(int64_t *words, int64_t reference);

/**
 * Calls the interpreter `Machine.install` handed over with the address of a request, and answers its word. A panic
 * without one: the kernel only calls back while a program runs.
 */
int64_t torb_machine_call_back(int64_t request);

/**
 * `test` and `group` as the table of thunks calls them inside the VM: the body is the address of the two words of the
 * program's closure, which the kernel runs through the interpreter behind the runtime's own recovery point.
 */
void torb_machine_test_case(torb_text name, const int64_t *body);
void torb_machine_test_group(torb_text name, const int64_t *body);

/** A location the VM defined, by its index; `torb_location_unknown` for -1. */
torb_location torb_machine_location(int64_t index);

double torb_machine_double_of(int64_t word);

/** `Process.arguments()` of the program the VM runs, which the table of thunks calls instead of the runtime's. */
torb_list torb_machine_process_arguments(void);
/** `Process.executablePath()` of the program the VM runs: its entry file, where the run named one. */
bool torb_machine_process_executable_path(torb_text *out);
int64_t torb_machine_word_of_double(double value);

#endif /* TORB_MACHINE_H */
