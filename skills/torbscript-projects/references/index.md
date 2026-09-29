# The pages of torbscript-projects

Every page of this skill, with what it answers. Search this file for a word, then open the one page that
answers the question. A page marked planned describes a feature that does not compile yet.

## Contents

- language/modules-and-packages
- standard-library
- how-to
- tooling

## language/modules-and-packages

- `language/modules-and-packages/packages.md` - **Packages** (reference): A package is a directory with a project.trb, named owner/name, whose file names say what it produces - src/lib.trb, src/main.trb, *.test.trb - and it can only be reached by a project that lists it as a dependency.
- `language/modules-and-packages/workspaces.md` - **Workspaces** (reference): A workspace is one root project.trb naming several member projects that check, build and test as a group, and resolve their dependencies into one shared project.lock.trb.

## standard-library

- `standard-library/project.md` - **std/project** (package): The receiver type of project.trb - Project, Dependencies, Program, Profile, Test, Tasks, Workspace, Registry and Source.

## how-to

- `how-to/build-a-native-binary.md` - **Build a native binary** (how-to): Run torb build in a package or point it at a file, look at the generated C with --emit-c first if a C compiler is not on the machine yet, and read what the back end does not lower yet before you debug the program instead.
- `how-to/add-a-dependency.md` - **Add a dependency** (how-to): Declare the package in project.trb before importing from it, tell a runtime dependency from a development one, and read what the imports of everything you depend on say it can reach.
- `how-to/set-up-a-workspace.md` - **Set up a workspace** (how-to): Name the member directories in the root project.trb, give each one its own project.trb, and depend on a sibling by name alone - the workspace resolves it from source.

## tooling

- `tooling/project-trb.md` - **project.trb** (tooling): The manifest of a project - its name, dependencies, workspace, further programs and profiles. The names of its files say what it produces, and the settings that decide its files are read before anything runs.
- `tooling/project-lock-trb.md` - **project.lock.trb** (tooling): The locked manifest - the exact version, tree hash and registry of every package a workspace depends on, and the evaluated settings of each of its packages - written deterministically and read without running anything.
- `tooling/torb-add.md` - **torb add** (tooling): torb add writes a dependency into project.trb, resolves the workspace with it, writes project.lock.trb and installs what is new - or changes nothing and explains why there is no solution.
- `tooling/torb-remove.md` - **torb remove** (tooling): torb remove takes a dependency out of project.trb, resolves the workspace again, and drops from project.lock.trb every package nothing needs any more.
- `tooling/torb-update.md` - **torb update** (tooling): torb update resolves every package of the workspace, or only the named ones, to the highest version project.trb allows, writes project.lock.trb, and refuses an update that gains a capability unless --accept-capabilities says it is wanted.
- `tooling/torb-install.md` - **torb install** (tooling): torb install fetches every package project.lock.trb pins that is not in the cache yet, checks its tree hash before a file is written, and never changes the lock.
- `tooling/torb-publish.md` - **torb publish** (tooling): torb publish builds and checks a package's archive as a registry receives it, prints its tree hash and capabilities, and writes it into a directory registry or uploads it with a token or through trusted publishing.
- `tooling/torb-lock.md` - **torb lock** (tooling): torb lock writes the settings block of every member of a workspace into project.lock.trb from its evaluated manifest and keeps the graph as it is; with --check it writes nothing and fails where the file is not what it would write.
- `tooling/torb-doc.md` - **torb doc** (tooling): torb doc turns the public API of a package and its doc comments into a reference - a static site, or one JSON document for an editor and the registry - and runs the examples of the doc comments as doc tests.
