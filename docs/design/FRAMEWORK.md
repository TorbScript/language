# The Application Framework

**Status: planned** — nothing of it exists, and it comes last in milestone 10 ([ROADMAP.md](../ROADMAP.md)), after
the network, HTTP and the formats it stands on. This record is a stub that keeps the guideline decided on 2026-09-19;
the design comes before any of it is built.

**`std` provides an application framework with the scope of Spring Boot or Symfony - dependency injection, a
hexagonal architecture, MVC, an ORM - and with their cleanliness and flexibility, built the way TorbScript works.** A
developer who knows Spring or Symfony should find their way around quickly, even though the mechanisms underneath are
different.

## 1. The guideline

- **The same words and the same layers**: controller, service, repository, configuration with profiles, middleware
  and filters, events, migrations, and test slices that start one layer without the others.
- **The same scope.** It is not a micro framework that leaves persistence, configuration and testing to the reader.
- **Wired at compile time, never by reflection.** The language has no annotations and no reflection by design, so there
  is no classpath scanning and no container that discovers components at run time. Dependency injection is **wiring
  through traits and constructors**: a service names what it needs as the traits of its fields, and a module assembles
  the program with a DSL on receiver closures, as `project.trb` does. A missing dependency is a compile error at the
  line that forgot it, not a failure at startup.
- **Routes and mappings are DSLs too.** An MVC route and an ORM mapping are written in a builder, and a query is a
  quoted expression (`Expression<Value>`): `examples/query-provider` already translates `filter { _.age >= minAge }`
  to SQL, and it is the seed of the ORM.
- **Connections are URIs.** The connection to a database, a cache or a message broker is a URI whose scheme chooses
  the driver ([URI.md](URI.md) section 11).

This is the one place where "the level of Spring Boot" will look different from Spring, and deliberately so.

## 2. What exists to build on

- [WEB.md](WEB.md) - handlers, routes as types with cases, middleware, HTML and a live UI. It decides that
  dependencies are captured values (its section 3.1), which is the constructor wiring above, and it leaves the ORM, the
  container and migrations to this record.
- [CONCURRENCY.md](CONCURRENCY.md) and [STREAMS.md](STREAMS.md) - tasks and streaming bodies.
- [ENCODING.md](ENCODING.md) - request and response bodies, configuration files, database rows.
- [SCRIPTS.md](SCRIPTS.md) - configuration as a receiver script, run in a sandbox.

## 3. Before it is built

A design with **one example carried through every layer** - a REST controller, the service behind it, a repository and
the database - so that its readability for somebody coming from Spring or Symfony can be judged before the framework
exists.
