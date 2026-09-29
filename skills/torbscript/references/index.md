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

- `guide/index.md` - **Learn TorbScript** (index): A guide for a programmer who already knows another language - a 15-minute tour, then one idea per page, finishable in an evening.
- `guide/the-language-in-sixty-seconds.md` - **The language in sixty seconds** (guide): The mental model of TorbScript in one screen: values, bindings, no null, no exceptions, traits, and calls written as commands.
- `guide/torbscript-in-15-minutes.md` - **TorbScript in 15 minutes** (guide): The fastest honest tour of TorbScript for a working programmer, one short example and a few sentences per idea.
- `guide/installing-and-running.md` - **Run your first program** (guide): Install the toolchain, run a single file, and create a project with a manifest, a source file and a test.
- `guide/values-and-bindings.md` - **Values and bindings** (guide): Why const and var are the whole mutation story, what a copy costs, and the one trap that catches everybody coming from a language with references.
- `guide/functions-and-closures.md` - **Functions and closures** (guide): How to declare a function, when it must spell out its return type, and the one closure form the language has.
- `guide/types-and-methods.md` - **Types and methods** (guide): How to declare a type, add methods to it, and tell a verb that changes it from the participle that answers a copy.
- `guide/cases-and-matching.md` - **Cases and matching** (guide): How to declare a type with more than one shape, and take it apart with a match that has to cover every case.
- `guide/traits.md` - **Traits** (guide): How to declare a capability, give it to a type, and use the trait itself as a type that hides which concrete type it is.
- `guide/errors.md` - **Errors** (guide): How a function says it can fail with Result, and how a caller handles that with match or the question mark operator.
- `guide/collections-and-pipelines.md` - **Collections and pipelines** (guide): How to build a list, map and set, change one in place or get a changed copy, and pull values through a lazy pipeline.
- `guide/control-flow-and-dsls.md` - **Control flow and your own constructs** (guide): if, for, while and loop as you would expect, and why unless is an ordinary function you could have written yourself.
- `guide/modules-and-packages.md` - **Modules and packages** (guide): How use brings a name in from another file or the standard library, and what public means for a top-level declaration.
- `guide/tests-and-tooling.md` - **Tests and the toolchain** (guide): How to write a test with test, group and assert, and the two commands that check whether what you wrote is correct.
- `guide/a-small-program.md` - **Put it together** (guide): One small program - a type with cases, a function that can fail, and a pipeline - that uses everything this path taught.
- `guide/idiomatic-torbscript.md` - **Idiomatic TorbScript** (guide): The habits that make TorbScript read like TorbScript - names, mutation, calls, types, errors, closures, resources and tasks - each as one rule, one runnable example and the reason behind it.

## guide/coming-from

- `guide/coming-from/index.md` - **Coming from another language** (index): One page per language - a table of the 10 to 15 things that map directly, the 5 that will surprise you, and what is deliberately missing.
- `guide/coming-from/rust.md` - **Coming from Rust** (contrast): The 15 things that map directly from Rust, the 5 that will surprise you, and what Rust has that TorbScript deliberately does not.
- `guide/coming-from/typescript-javascript.md` - **Coming from TypeScript and JavaScript** (contrast): The 15 things that map directly from TypeScript and JavaScript, the 5 that will surprise you, and the type-level tricks that have no counterpart.
- `guide/coming-from/python.md` - **Coming from Python** (contrast): The 15 things that map directly from Python, the 5 that will surprise you, and what static, value-typed code gives up on dynamism.
- `guide/coming-from/go.md` - **Coming from Go** (contrast): The 15 things that map directly from Go, the 5 that will surprise you, and the concurrency primitives that are not built yet.
- `guide/coming-from/kotlin-swift.md` - **Coming from Kotlin and Swift** (contrast): The 15 things that map directly from Kotlin or Swift, the 5 that will surprise you, and the property and coroutine machinery neither one keeps.

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
