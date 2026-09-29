# The pages of torbscript

Every page of this skill, with what it answers. Search this file for a word, then open the one page that
answers the question. A page marked planned describes a feature that does not compile yet.

## Contents

- guide
- guide/coming-from
- language/syntax
- how-to
- explanation
- tooling

## guide

- `guide/index.md` - **Learn TorbScript** (index): For people who already program - a fifteen-minute tour, then one short page per idea, all of it done in an evening.
- `guide/tour.md` - **A tour of TorbScript** (guide): The whole language in fifteen minutes for somebody who already programs - bindings, calls, functions, types, cases, errors, traits and pipelines, one short example each.
- `guide/installing-and-running.md` - **Install and run** (guide): Install TorbScript with one command, run a file, and make a project with a test - the five commands you will use every day.
- `guide/values-and-bindings.md` - **Values and bindings** (guide): A binding is const or var, a second name is always a copy, and a change goes through the path where the value lives.
- `guide/functions-and-closures.md` - **Functions and closures** (guide): Declare a function with typed parameters, defaults and labels, write a closure, and pass it as the last argument of a call.
- `guide/types-and-methods.md` - **Types and methods** (guide): Declare a type with fields, give it methods, and tell a method that changes the value from one that returns a changed copy.
- `guide/cases-and-matching.md` - **Cases and matching** (guide): Declare a type whose value is one of several cases, and take it apart with a match that has to handle every case.
- `guide/traits.md` - **Traits** (guide): Declare what a type can do as a trait, give it to a type now or later, and use the trait as a type that holds any of them.
- `guide/errors.md` - **Errors** (guide): A function that can fail returns a Result, a value that can be missing is an Option, and the caller handles both with match, the question mark or a fallback.
- `guide/collections-and-pipelines.md` - **Collections and pipelines** (guide): Build a list, a map and a set, change one in place or get a changed copy, and run values through a pipeline of steps.
- `guide/control-flow-and-dsls.md` - **Control flow and your own constructs** (guide): Ifs and loops work as you expect, and a new control structure or a configuration block is an ordinary function you can write yourself.
- `guide/modules-and-packages.md` - **Modules and packages** (guide): Split a program into files with public and use, import from the standard library, and lay out a project so that its tests reach its code.
- `guide/a-small-program.md` - **Put it together** (guide): One small program - a type with cases, a type with fields, a function that can fail and a pipeline - that uses what the guide taught.
- `guide/idiomatic-torbscript.md` - **Idiomatic TorbScript** (guide): The habits that make code read like the standard library - whole-word names, values before shared types, a checked door for every rule a value must keep, using for resources, and the formatter's layout.

## guide/coming-from

- `guide/coming-from/index.md` - **Coming from another language** (index): One page per language you may know - what maps directly, the habits that will trip you up, and what TorbScript leaves out.
- `guide/coming-from/rust.md` - **Coming from Rust** (contrast): What maps directly from Rust, the five habits that will trip you up, and what Rust has that TorbScript leaves out on purpose.
- `guide/coming-from/typescript-javascript.md` - **Coming from TypeScript and JavaScript** (contrast): What maps directly from TypeScript and JavaScript, the five habits that will trip you up, and the type tricks that have no counterpart.
- `guide/coming-from/python.md` - **Coming from Python** (contrast): What maps directly from Python, the five habits that will trip you up, and what a language checked before it runs leaves out.
- `guide/coming-from/go.md` - **Coming from Go** (contrast): What maps directly from Go, the five habits that will trip you up, and the concurrency tools that are not built yet.
- `guide/coming-from/kotlin-swift.md` - **Coming from Kotlin and Swift** (contrast): What maps directly from Kotlin or Swift, the five habits that will trip you up, and the property and coroutine features TorbScript leaves out.

## language/syntax

- `language/syntax/cheat-sheet.md` - **Syntax cheat sheet** (reference): Every form of the language in one place: declarations, expressions, patterns, types and the call rules, with the exact spelling of each.

## how-to

- `how-to/index.md` - **Task recipes** (index): One page per task for somebody who already knows the language: the steps, the pitfalls, and one complete program that works.
- `how-to/set-up-your-editor.md` - **Set up your editor** (how-to): Install the VS Code extension from the Marketplace, Open VSX or a .vsix - it installs a missing toolchain and brings the language server, the Test Explorer, the debugger and the formatter - or point an editor at torb lsp and torb debug.

## explanation

- `explanation/mistakes-models-make.md` - **What a model trained on other languages gets wrong** (explanation): The mistakes a language model makes in TorbScript because it has read Rust, Swift, Kotlin and TypeScript, each with the wrong line, the right line and the diagnostic.

## tooling

- `tooling/index.md` - **The toolchain** (index): The torb command, the project files, and how to verify that what you wrote is correct and in the formatter canon.
- `tooling/the-torb-command.md` - **The torb command** (tooling): Every subcommand of the toolchain, what it does today, and which of them are still planned.
- `tooling/verifying-your-work.md` - **Verify your work** (tooling): The commands that decide whether TorbScript you wrote is correct and in the layout of the formatter, in the order to run them.
- `tooling/torb-new.md` - **torb new** (tooling): torb new scaffolds a package or app from a template - git.torb.dev's package or app by default - or, offline, the built-in project.trb, src/main.trb and tests/main.test.trb; torb init does the same into the current directory.
- `tooling/torb-init.md` - **torb init** (tooling): torb init fills the current, empty-enough directory with a template - or the built-in scaffold - the way torb new fills a new one, and refuses to overwrite a file that is already there.
- `tooling/torb-check.md` - **torb check** (tooling): torb check resolves every module, import and name in a type position, types every expression, and reports one block per diagnostic - the gate every other command trusts.
- `tooling/torb-run.md` - **torb run** (tooling): torb run runs a file, the only program below a directory or the program named in the bytecode VM, or builds and runs it natively with --native, passing the rest of the command line, the three streams and the exit code through.
- `tooling/torb-repl.md` - **torb repl** (tooling): torb repl reads entries from standard input, checks each against the session, runs it in the bytecode VM and keeps what it binds and declares for the next entry - a typed session and a piped file behave the same.
- `tooling/torb-lsp.md` - **torb lsp** (tooling): torb lsp is the language server - the compiler over the Language Server Protocol - with the diagnostics of torb check for the whole workspace, hover from torb doc, references, a checked rename, completion, formatting and lint fixes.
- `tooling/torb-debug.md` - **torb debug** (tooling): torb debug is the debugger - a debug adapter over the Debug Adapter Protocol on standard input and output - with breakpoints, stepping, the call stack, locals and evaluate for a program or a test run in the VM.
- `tooling/torb-build.md` - **torb build** (tooling): torb build type checks every program below a path, or the one named, lowers each to C and hands the C to whatever compiler it finds - a library is checked and builds nothing.
- `tooling/torb-canon.md` - **torb canon** (tooling): torb canon is deprecated - it runs torb format now, with a warning - and the formatter canon it enforced, every rule of it, is what torb format runs first, over the syntax tree and with a safety net.
- `tooling/torb-format.md` - **torb format** (tooling): torb format writes TorbScript sources in the one layout of the language - the rules of the formatter canon, then indentation, spaces, blank lines and a width of 120 columns - and --check fails on every file that is not in it.
- `tooling/torb-lint.md` - **torb lint** (tooling): torb lint reports the style rules the type checker leaves alone - Self, a question field, an unread binding, an unlabeled literal, a redundant Some, private(var) - and the uses of deprecated declarations, with a fix where certain.
- `tooling/torb-rename.md` - **torb rename** (tooling): torb rename renames fields at their declaration and at every use the type checker resolves to them - members, bare names, labels, patterns and doc links - or refuses and writes nothing.
