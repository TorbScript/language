# First-Party Packages

**Status: decided 2026-09-29; `torb/sql` 0.1.0 and `torb/postgres` 0.1.0 built and tested, `torb/redis` 0.1.0
published** — the owner asked on 2026-09-28 for packages the project offers itself that do not belong in `std`:
adapters for databases, key-value stores and document stores, "everything TorbScript will surely need for full-scale
development". This record decides the vendor they are published under, the line between them and `std`, how their
repositories look, and the order they come in; section 6 and 7 record the first two as built, and section 8 lists what
`std` lacks for all of them, as input for the rounds of `std`. Nothing here is published yet except `torb/redis`.

**A first-party package is an adapter to a system outside the program, published by the project under the owner
`torb`, in a repository of its own, with its own version.** `std` stays what every program may need and what reaches
no service; the moment a package speaks the protocol of somebody else's server, it leaves `std` for `torb/*`, where it
can follow that server's releases instead of the language's.

```text
   a program          use Pool from "torb/sql"          use Postgres from "torb/postgres"
       │                          │                                │
   torb/*             torb/sql ── values, rows, errors, Pool, Transaction, the traits a driver implements
   (git.torb.dev/torb, │           ▲            ▲            ▲
    packages.torb.dev) │      torb/postgres  torb/mysql  torb/sqlite          torb/redis   torb/mongodb   torb/s3
       │               │           │            │            │                    │            │            │
   std                 std/network, std/tls, std/digest, std/binary, std/encoding, std/uri, std/storage, std/task
```

## Contents

- **[1. Goals](#1-goals)**
- **[2. The vendor](#2-the-vendor)** — `torb`, and the three it was weighed against
- **[3. The line between `std` and `torb/*`](#3-the-line-between-std-and-torb)**
- **[4. Repository conventions](#4-repository-conventions)** — template, layout, CI, publishing, versions, support
- **[5. The order](#5-the-order)** — the packages, with reasons and what each waits for
- **[6. `torb/sql`, as built](#6-torbsql-as-built)**
- **[7. `torb/postgres`, as built](#7-torbpostgres-as-built)**
- **[8. What `std` is missing](#8-what-std-is-missing)** — the gaps, precisely, as input for rounds of `std`
- **[9. What building them found in the toolchain](#9-what-building-them-found-in-the-toolchain)**
- **[10. Slices](#10-slices)**
- **[11. Open](#11-open)**

## 1. Goals

1. **A program talks to the services it runs against without leaving TorbScript**: Postgres, MySQL, SQLite, Redis,
   MongoDB, object storage, a message broker, mail - each one import away, each one idiomatic: errors as values,
   `Task`s for everything that waits, full words in every name.
2. **Pure TorbScript over `std`, no C**, until `foreign` is decided (RELEASE.md section 2): a package of the registry
   carries source and nothing else (RELEASE.md section 7.2), so a driver is the wire protocol written out, like
   Go's `pgx` or Rust's `sqlx`, not a binding to `libpq`.
3. **One shape per kind of service.** Every SQL driver implements the traits of `torb/sql`, so the pool, the
   transactions, the rows and the errors are written once and a program changes databases by changing a line; the
   connection string is a URI whose scheme chooses the driver (URI.md section 11).
4. **Each package follows its service, not the language**: its own semantic version and cadence, a support policy
   that names the versions of the service it is tested against, and CI against the real service in a container.
5. **What the packages need from `std` is found by building them**, and written down (section 8), rather than
   guessed: a gap is closed in `std` once two packages worked around it.

## 2. The vendor

**Decision: the owner `torb`** — `torb/postgres`, `torb/redis`, `torb/sql` — and the repositories in the organisation
`torb` of git.torb.dev, where the path of a repository is the name of its package (`git.torb.dev/torb/postgres`).

| Owner | For | Against |
|---|---|---|
| **`torb`** | short, and it is the command a user types all day; the brand (BRAND.md) and the domain (`torb.dev`); reserved on the registry already (RELEASE.md section 7.1), so nobody else can take it; `torb/postgres` reads as "the project's Postgres driver" without a word saying so | a user may take `torb/*` for part of the toolchain - the README of every package says it is not, and its own version says so too |
| `torbscript` | the language's full name; the owner the toolchain's own packages use (`torbscript/compiler`, `torbscript/registry`) | long in every `use` line; and it is the toolchain's owner - an adapter to Postgres is not part of the toolchain, and should not look like it |
| `x`, after Go's `golang.org/x` | the shortest; a known convention for "maintained by the project, outside the standard library" | a letter says nothing to anybody who does not know Go, and what it means there - developed "under looser compatibility requirements than the Go core" - is the opposite of what an adapter the project stands behind should promise; one letter is also one edit away from every other one-letter owner |
| `official` | says it outright | a word, not a name: it claims a status rather than naming an owner, invites `0fficial` and `officiaI` squatting, and ages badly once a package is deprecated |

The toolchain's packages stay `torbscript/*`, and `std/*` stays the toolchain's own: three owners, three meanings -
`std` comes with `torb` and needs no line in `project.trb`, `torbscript` is the toolchain, `torb` is what the project
publishes for programs to depend on.

## 3. The line between `std` and `torb/*`

**Decision: `std` holds what every program may need, reaches no external service, and follows the language's release
and its compatibility promise; `torb/*` holds adapters to external systems, each with its own semantic version and
cadence.** One question decides a case: *does the package speak the protocol of a server somebody else writes?* If it
does, its correctness depends on that server's releases, its tests need that server, and its version has to move with
it - so it cannot ride the language's one version number (RELEASE.md section 3).

| | `std` | `torb/*` |
|---|---|---|
| What | the vocabulary every program shares: values, collections, text, formats, the network and TLS as transports, HTTP | adapters to systems outside the program: databases, caches, brokers, object stores, mail, identity providers |
| External service | none - a test needs nothing but the toolchain | one, and CI runs it in a container |
| Version | the toolchain's, one number (RELEASE.md section 3) | its own semver; `0.x` while its API settles |
| Cadence | the language's releases | the service's: a new Postgres major can mean a release that week |
| Compatibility | the language's promise, once 1.0 is out | its own, per major version |
| In `project.trb` | nothing: every `std/*` comes with `torb` | a `runtime "torb/postgres:^0.1.0"` line |
| Capability | named by the package (`std/network` is `network`) | whatever its imports reach, shown by `torb publish` and `torb add` |

**What that puts where:**

| Package | Where | Why |
|---|---|---|
| a format with no service - JSON, YAML, TOML, XML, Protocol Buffers | `std` | ROADMAP milestone 10 already lists them; a format is vocabulary |
| HTTP, TLS, DNS | `std` | transports every program may need; done (NETWORK.md, DNS.md) |
| `Storage` with `file:` and `memory:` | `std` | URI.md's rule: a capability trait belongs in `std` when `std` ships two drivers for it, one of which reaches the outside world |
| `s3:`, `webdav:` drivers of `Storage` | `torb/*` | each speaks one vendor's API |
| Postgres, MySQL, SQLite, Redis, MongoDB | `torb/*` | each speaks one server's protocol |
| the traits and the pool every SQL driver shares | `torb/sql` | a trait with no driver in its own package is a specification nobody has run (URI.md section 11); `std` ships no SQL driver, so the traits live beside the drivers |
| NATS, AMQP, SMTP | `torb/*` | services |
| JWT and JOSE | `torb/jwt` | no service, but a list of algorithms that security advisories change on their own cadence, and crypto `std` does not have yet (section 8) |
| OAuth 2.1 and OpenID Connect | `torb/oauth` | talks to an identity provider |
| gRPC | `std`, as ROADMAP milestone 10 says | a transport like HTTP, over HTTP/2 in `std/http`; the owner's brief listed it with the adapters, and the line puts it back: no server of somebody else is spoken to that HTTP does not already speak to |

**A package moves from `torb/*` into `std` only when the line moves**: if a protocol becomes something every program
needs and a service nobody runs (HTTP once was a service), it is proposed for `std` with the reasons, and `torb/*`
keeps a last version that re-exports it. Nothing moves the other way after 1.0: that would break a promise.

**URI.md's `Connection`, `Connect` and `Cache`** were written out "so that the ecosystem converges on one spelling"
and left out of `std`. `torb/sql` takes the first two, shaped for SQL (section 6): its `Connection` has the query
surface URI.md said a generic one cannot carry, because every SQL database has the same one. `Cache` waits for a
second key-value driver next to `torb/redis`; until then `torb/redis` has its own types.

## 4. Repository conventions

**Every package is its own repository in the organisation `torb` of git.torb.dev, made from the package template**
(`torb/package-template`, mirrored to GitHub): the template's CI, its trusted publishing on a tag `v*`, its MIT
license, its README skeleton.

```text
torb/postgres/
├─ project.trb           name "torb/postgres", version, description, license "MIT", repository, language, authors
├─ project.lock.trb      committed: the dependencies a check resolves against
├─ src/lib.trb           the library; every public name exported from here, with a doc comment
├─ tests/unit/           no service: the codec, the types, a double of the server on loopback
├─ tests/integration/    against the real service, in a container
├─ tests/fixtures/       certificates and the like for the tests
├─ scripts/<service>.sh  starts the service in Docker for a developer, the same image CI uses
├─ examples/             programs a reader runs by hand
├─ README.md             what it is, connecting, the type mapping, what is not supported, developing, publishing
├─ LICENSE               MIT
├─ .forgejo/workflows/   ci.yml, publish.yml
└─ .github/workflows/    the same, for the mirror
```

| Question | Decision | Why |
|---|---|---|
| The service in CI | a service container of the job (`services:`), with the image the package's support policy names; the integration job runs `torb test tests/integration` | the container is started by the runner before the steps, and both forges support it; a script that runs `docker run` from inside a job reaches nothing on Forgejo, whose jobs run in a container |
| Where the service answers | on Forgejo at its service name on its own port (`postgres:5432`), because the job runs in a container on the services' network; on GitHub at `localhost` and the mapped port, because the job runs on the host | found by `torb/redis`'s first CI run; the two workflows differ in one variable |
| Two test directories | `tests/unit` needs nothing, `tests/integration` needs the service | `torb test tests/unit` runs everywhere, a laptop without Docker included; the `ci` job runs it, the `integration` job the other |
| A developer's service | `scripts/<service>.sh start` runs the same image in Docker on a port of its own and prints the variables the tests read | the tests read `TORB_<SERVICE>_URI`, and default to the script's, so `torb test` needs no configuration |
| What the service container cannot do | a feature that needs files in the server - TLS with a test certificate, `pg_hba.conf` lines - runs where the script started the server, and is tested against a double of the server on loopback in `tests/unit` everywhere | a service container starts before the checkout, so it cannot mount the repository's files |
| Publishing | a tag `v*` runs `publish.yml`, which runs `torb publish` with the job's OpenID Connect token (trusted publishing, RELEASE.md section 7.11); no token is stored | the template's; section 10 has the publishers each package needs |
| A dependency between `torb/*` packages | through the registry, `^` of the published version; during development a `source "torb/sql", path: "../sql"` line, and the CI clones the other repository beside the checkout while that line is there | `torb publish` refuses a dependency from a path; the line goes, `torb update` runs and the lock is committed before the first tag |
| Versions | semver, starting at `0.1.0`; a `0.x` minor may break, a patch never does; `1.0.0` once the API has not changed for two minor releases and the service's current versions are all supported | RELEASE.md section 3's rule for the toolchain, applied per package |
| `language` | the oldest toolchain the package is tested with, in `project.trb` (PROJECT.md section 10); raised only in a minor release | an older `torb` refuses the package with a message instead of failing to check it |
| Owners on the registry | the organisation `torb`, created with the operator's token because the owner is reserved (`docs/contributing/releasing.md`, "The first accounts"); the owner and the maintainers are its members | one owner for every first-party package, so trusted publishers and yanks are configured in one place |

**The support policy**: a package supports the versions of its service that the service's own project supports, and
CI runs against the newest; the README says which. For the first packages:

| Package | Supported | Tested in CI | Why |
|---|---|---|---|
| `torb/postgres` | PostgreSQL 13 to 17 (the PostgreSQL project supports a major for five years) | `postgres:17-alpine`, the image of the owner's server | the protocol 3.0 is the same since 7.4; SCRAM since 10; everything used is in 13 |
| `torb/redis` | Redis 6 and later, Valkey 7.2 and later | `redis:7-alpine`, `valkey/valkey:8-alpine` | RESP3 through `HELLO 3` since Redis 6, RESP2 before |
| `torb/mysql` | MySQL 8.0 and 8.4 LTS, MariaDB 10.11 and 11 LTS | the newest of each | `caching_sha2_password` is MySQL 8's default |
| `torb/mongodb` | the MongoDB versions MongoDB, Inc. supports, from 6.0 | the newest | `OP_MSG` since 3.6, SCRAM-SHA-256 since 4.0 |

## 5. The order

**Decision: the SQL foundation and Postgres first, then the rest of what a web service stands on, in this order.**
Each row says why it is where it is, and what it waits for.

| # | Package | What | Why here | Waits for |
|---|---|---|---|---|
| 1 | `torb/sql` | the shared SQL layer: `SqlValue`, `Row`, `Column`, `SqlError`, `ServerReport`, the traits `Database`, `Connection`, `Connect`, a `Pool`, `Transaction` - Go's `database/sql` and the traits of Rust's `sqlx` | every SQL driver after it is smaller for it, and a program is written against it once | nothing - **built**, section 6 |
| 2 | `torb/postgres` | the first driver: protocol 3.0, SCRAM-SHA-256, md5, TLS, the simple and the extended protocol, the types, notices, `LISTEN`/`NOTIFY` | the database of the owner's server and the first choice of new services; the protocol is documented to the byte | nothing - **built**, section 7 |
| 3 | `torb/redis` | Redis and Valkey: RESP2 and RESP3, pipelining, `MULTI`/`EXEC`, pub/sub, a pool | the cache and queue beside almost every database; a small text protocol | nothing - **published 0.1.0**, by another round |
| 4 | `torb/mysql` | MySQL and MariaDB: protocol 4.1, `caching_sha2_password` and `mysql_native_password`, TLS through the SSL request, the binary protocol for parameters | the other half of the SQL world; the second driver is what proves `torb/sql`'s traits | SHA-1 in `std/digest` (section 8); RSA encryption for `caching_sha2_password` without TLS, or TLS required until then |
| 5 | `torb/mongodb` | the document store: BSON as a `Format` of `std/encoding`, `OP_MSG`, SCRAM, `mongodb+srv:` through SRV and TXT records | the most used document store; BSON is the one format here that is a service's own | SHA-1 for SCRAM-SHA-1, HMAC and PBKDF2 in `std/digest`; `std/network`'s `lookup` has SRV and TXT already |
| 6 | `torb/s3` | a driver of `std/storage`'s `Storage` for the scheme `s3:` - AWS S3 and everything that speaks its API (MinIO, R2, Hetzner's object storage) | URI.md section 11 and ROADMAP milestone 10 name it as the first `Storage` driver outside `std`; the registry's backups already use S3-compatible storage | HMAC-SHA-256 and the wall clock for Signature Version 4, an XML reader for the listings (section 8) |
| 7 | `torb/sqlite` | SQLite, in the process | the embedded database every tool wants | **the `foreign` decision** of RELEASE.md section 2: whether a registry package may declare `foreign` functions and carry C. SQLite is C; a TorbScript reimplementation of its file format and query engine is out of the question, and a native of `runtime/` is refused (RELEASE.md section 7.11: a nine-megabyte C file in every build). Until then it cannot be built |
| 8 | `torb/nats`, `torb/amqp` | messaging: NATS's text protocol with NKey signatures; AMQP 0-9-1 for RabbitMQ | services of every size of system; NATS is small, AMQP is RabbitMQ | Base32 for NKeys (Ed25519 is in `std/signature`); nothing for AMQP |
| 9 | `torb/smtp` | sending mail: SMTP with `STARTTLS`, `AUTH PLAIN` and `LOGIN`, MIME messages | every service sends mail | Base64 exported by `std/encoding`, the wall clock for the `Date` field |
| 10 | `torb/jwt`, `torb/oauth` | JOSE (JWS, JWK, JWT) and an OAuth 2.1 / OpenID Connect client | sign-in for a web service; `tools/registry` already verifies RS256 tokens in TorbScript | HMAC, RSA and ECDSA verification in `std/signature`, the wall clock, cryptographic randomness for `state` and PKCE |
| - | protobuf, gRPC | not `torb/*`: `std`, as ROADMAP milestone 10 says (section 3) | | HTTP/2 and ALPN |

## 6. `torb/sql`, as built

**What it is: the layer Go's `database/sql` is, with the traits Rust's `sqlx` has, in the shape URI.md section 11
wrote out.** It imports `std/uri`, `std/encoding`, `std/task` and `std/time` and reaches nothing: its publish summary
says `clock`, and only because `Timestamp` lives in `std/time` (gap 10 of section 8).

| Name | What |
|---|---|
| `SqlValue` | a value of a column or a parameter: `Null`, `Bool`, `Integer`, `Real`, `Numeric` (digits as text), `Text`, `Binary`, `Timestamp`, `LocalTimestamp`, `Date`, `Uuid`, `Json`, `Array`; a parameter is written `[.Integer(7), .Text("ada")]` |
| `Row`, `Column` | a row: `row.value("name")`, `row.get<Int>("id")` for any `Decode` type, `row.decoded<User>()` for a whole row |
| `SqlError`, `ServerReport` | one wrapper with questions - `isServerError()`, `isUniqueViolation()`, `isTransactionConflict()`, `isConnectionFailure()`, `isRetryable()` - and the server's report with its SQLSTATE and fields |
| `Database` | what a statement runs against: `query` answers rows, `execute` a count. A connection, a pool and a transaction are each one |
| `Connection` | one session: `ping`, `isReusable`, `abandonTransaction` |
| `Connect` | a driver, with `Schemes`: `connect(uri)`; `Connect.registry([...])` over several |
| `Pool` | connections through any driver, `maximumConnections`, `acquireTimeout`, `begin()` |
| `Transaction` | `commit()`, `rollback()`; released unfinished, it rolls back |
| `Date`, `Uuid` | a day of the calendar and an identifier of RFC 9562, which `std` has no type for yet |
| `Permits` | a semaphore for tasks, which `std/task` has not (gap 14) |

**Decisions, and what they were weighed against:**

| Question | Decision | Instead of | Why |
|---|---|---|---|
| Where the pool is | in `torb/sql`, once, over `Connect` | one pool per driver (`sqlx`'s `PgPool`, `MySqlPool`); a generic pool in `std/task` | Go's choice: every driver gets it by implementing `Connection`; a generic pool in `std` is gap 15 - three pools exist now (`std/http`, `torb/redis`, `torb/sql`), and the one in `std` can come once they are compared |
| How a row becomes a type | `row.decoded<User>()` through `std/encoding`'s `Decode`: the row becomes a record of `EncodedValue`s and `ValueDecoder` reads it, the field `createdAt` from the column `created_at` (`Naming.SnakeCase`, or `.Unchanged`) | a `FromRow` trait of its own; a macro | `Decode` is derived for every data type, and ENCODING.md says a database driver is one more format; `row.get<Int>("id")` is the same machinery for one column |
| How a parameter is written | `List<SqlValue>`, whose cases are written with a leading dot | a trait `ToSql` and a list of it; variadic arguments | a list of a trait cannot be written from mixed literals without conversions; `.Integer(7)` says the type at the call, as a column arrives |
| Placeholders | the driver's own - `$1` for Postgres, `?` for MySQL | rewriting one spelling into the other | a rewriter has to lex SQL (strings, comments, dollar quotes); Go leaves it to the driver too |
| Rows | read whole before the task finishes: `List<Row>` | a `Source<Row>` | a connection that streams rows is busy until the source is drained, which a pool and a transaction have to track; streaming is slice 3 of section 10 |
| A transaction left unfinished | the release of the `Transaction` rolls it back: a pooled connection is dropped, a driver's own connection rolls back before its next statement (`abandonTransaction`) | sending `ROLLBACK` from `close()` | `close()` cannot wait; a pool that drops the connection lets the server roll back, and nothing is committed either way |
| A statement that fails inside a transaction | fails the transaction: the statements after it are refused, `commit()` rolls back and answers the failure | letting the program commit what is left | Postgres refuses everything after a failed statement anyway; on MySQL, which does not, committing half a change is the bug this prevents |
| One statement at a time | a connection holds one permit of `Permits`; a statement of another task waits for it | a lock per driver | the protocol of every database is one conversation at a time |

**Tested**: 46 tests in both back ends - the values and their texts, `Date` and `Uuid`, rows read by name, as types and
whole, the failures, and the pool, transactions and the registry against a double of a driver (`tests/pool.test.trb`),
which is also the smallest complete example of a driver.

## 7. `torb/postgres`, as built

**What it is: a driver in TorbScript over `std/network` and `std/tls`, no C, about 2 500 lines.** The protocol 3.0 of
the Postgres manual's "Frontend/Backend Protocol", message by message.

| Part | As built |
|---|---|
| Connecting | TCP to the host (`connectTo` tries every address), then TLS where `sslmode` asks, the startup message (`user`, `database`, `application_name`, `client_encoding=UTF8`, `DateStyle=ISO`, and every other query parameter of the URI), then the authentication, then the server's parameters and its key, until `ReadyForQuery`; `connect_timeout` through `within` |
| Signing in | SCRAM-SHA-256 (RFC 5802, RFC 7677) without channel binding; md5; a password in the clear; trust. A refusal of class 28 is `isAuthenticationFailure()` in the server's words; Kerberos, GSSAPI, SSPI and `SCRAM-SHA-256-PLUS` alone are refused by name |
| TLS | the TLS request, then `TlsStream.connect` over the same `TcpStream`. `disable`; `prefer` (the default: TLS where offered, in the clear where the server declines or its certificate is not trusted); `require`, `verify-ca`, `verify-full` all verify the certificate against the host, because `std/tls` has no switch that turns the check off - a private root goes into `Postgres(tls: TlsSettings(trusted: [root]))` |
| Statements | `query` through the extended protocol (Parse, Bind, Describe, Execute, Sync in one write), always, so its parameters travel apart from the statement; `execute` without parameters through the simple protocol, which takes a script; `command` answers the tag |
| Types | every value in the text format; a column read by its OID, a parameter sent with its OID, text and JSON as OID 0 so the server takes their type from where they stand; README.md has the table |
| Errors | `ErrorResponse`'s fields become a `ServerReport` (severity untranslated where the server sends `V`, code, message, detail, hint, position, context, schema, table, column, data type, constraint); an error leaves the connection usable |
| Notices, notifications | kept as they arrive beside a statement: `takeNotices()`, `takeNotifications()`; `notification()` waits for the next one |
| Breaking | a statement that the network or a cancellation cuts off in the middle marks the connection broken (a guard released before the exchange completed); every later statement fails at once as closed, and a pool drops it |
| Not built | `COPY` (refused, the connection stays usable), the binary format, kept prepared statements, cancel requests, Unix sockets, several hosts, the `PG*` variables, SASLprep |

**Decisions:**

| Question | Decision | Instead of | Why |
|---|---|---|---|
| The format of values | text, both ways | binary for the types the driver knows (pgx) | the simple protocol only answers text, so one reader serves both protocols; text needs no `Describe` round trip to choose formats; an unknown type still arrives readable |
| `query` without parameters | the extended protocol too | the simple one, which is a round trip shorter | one path for every statement, and a second statement in a `query` is refused (`42601`) rather than silently answered |
| SCRAM, HMAC, PBKDF2, MD5 | in the package (`src/hmac.trb`, `src/md5.trb`, `src/scram.trb`), tested against the RFCs' vectors | waiting for `std/digest` | Postgres's default authentication is SCRAM; gaps 1, 2 and 4 of section 8 |
| PBKDF2's speed | its loop keeps SHA-256's state and block as `Array`s of words and runs exactly the two compressions an HMAC of 32 bytes needs | `std/digest`'s `Sha256` for every round | 4 096 rounds took 9.9 s in the VM and 0.45 s natively through `Sha256`; now 3.2 s and 0.05 s. Signing in in the VM is still slow (gap 2) |
| The client's nonce | 18 bytes of the system's randomness, from `randomQueryIdentifier()` of `std/network` nine times | a nonce from a clock | the only randomness `std` hands a package is sixteen bits at a time, meant for DNS (gap 5) |
| `sslmode=prefer` and an untrusted certificate | reconnect in the clear | fail | `prefer` protects against nobody in between either way - they can decline for the server - and libpq's `prefer` connects to such a server too |
| The pool | `torb/sql`'s | one of its own | section 6 |

**Tested, with Postgres in Docker** (`postgres:17-alpine`, started by `scripts/postgres.sh` with `md5` for host
connections and TLS with the test certificate): 53 unit tests - the codec, the types, the connection string, the
cryptography against RFC 1321, 4231, 7914 and 7677, and a double of a server on loopback for trust, the password in the
clear, md5, refusals, TLS through the TLS request, errors, notices, notifications and a connection that breaks - and
65 integration checks against the real server: SCRAM and md5 roles, a wrong password, a database that does not exist,
TLS with the root trusted and without, every type both ways, a timestamp in another session zone, counts, a script,
unique, not-null and foreign-key violations with their constraint, a syntax error's position, `COPY` refused, 20 000
rows, a value of a million bytes, notices, commit, rollback, a failed and an abandoned transaction, three statements at
once through a pool of two, a pooled transaction, the registry, `LISTEN`/`NOTIFY` between two sessions, and a statement
cut off by a timeout. All of it passes in the VM and natively. The CI's service container runs the same suite without
TLS.

## 8. What `std` is missing

Found by building `torb/sql` and `torb/postgres`, and by reading what the packages after them need. Each gap names who
needs it and what is done in its place until then.

| # | Gap | Where it belongs | Needed by | Until then |
|---|---|---|---|---|
| 1 | **HMAC** (over SHA-256, SHA-1, MD5) | `std/digest` | SCRAM (postgres, mongodb), S3's Signature Version 4, JWT `HS256`, SMTP `CRAM-MD5` | done: `Hmac<H: Hasher>`, `HmacSha256`, `HmacSha1`, `HmacMd5` in `src/hmac.trb`, keyed once and copied per message |
| 2 | **PBKDF2**, fast in the VM | `std/digest`, a native or a word-level implementation | SCRAM (postgres, mongodb) | done: `Hmac.pbkdf2` in `src/pbkdf2.trb`, keyed once and copied per round - roughly 7-11 s in the VM for 4 096 rounds on a shared, contended machine (half the time of the same rounds re-keyed naively, measured back to back), 0.2-0.3 s natively; `torb/postgres/src/hmac.trb`'s naive version: 3.2 s / 0.05 s on its own machine |
| 3 | **SHA-1** | `std/digest` | `mysql_native_password`, SCRAM-SHA-1 of MongoDB, WebSocket's accept key | done: `Sha1` in `src/sha1.trb`; `torb/mysql` and `torb/mongodb` still wait on the drivers themselves |
| 4 | **MD5**, marked as legacy | `std/digest` | Postgres's `md5` authentication, S3's `Content-MD5` | done: `Md5` in `src/md5.trb`, its doc comment marking it legacy - never for security |
| 5 | **Cryptographic random bytes** | `std/random`'s slice 2 or `std/os` (RANDOM.md) | every nonce: SCRAM, OAuth `state` and PKCE, MongoDB | `randomQueryIdentifier()` of `std/network`, sixteen bits a call |
| 6 | **RSA and ECDSA**: RSA-OAEP encryption, PKCS #1 v1.5 and PSS verification, ECDSA P-256 | `std/signature` | `caching_sha2_password` without TLS (mysql), JWT `RS256`/`ES256`, OpenID Connect | `tools/registry/src/rsa.trb` verifies RS256 in TorbScript, privately |
| 7 | **Base64 and hexadecimal** exported, both alphabets of RFC 4648 | `std/encoding` | SCRAM, SMTP `AUTH`, JWT, S3, MIME | done: `Base64Alphabet`, `base64Decoded` in `src/base64.trb`, `hexEncoded`/`hexDecoded` in `src/hexadecimal.trb`; `std/dns` and `std/yaml` use them now, and so does `tools/registry` - its private copy is gone |
| 8 | **Base32** | `std/encoding` | NATS NKeys, TOTP | done: `Base32Alphabet` (standard and extended/"hex") in `src/base32.trb` |
| 9 | **The wall clock**: the current `Timestamp` | `std/time` (`Clock.timestamp()` or `Timestamp.now()`) | JWT `exp` and `nbf`, S3's signing date, OAuth token expiry, SMTP's `Date` | `tools/registry` reads the time from SQLite (`database.now()`); `Clock.now()` is monotonic |
| 10 | **`Timestamp` without the clock capability**: the value type in the prelude, beside `Duration` and `Instant` | `std/prelude` | every package with a timestamp in a signature | `torb publish` reports `clock` for `torb/sql` because `SqlValue` has a `Timestamp` case |
| 11 | **Calendar types and parsing**: `Date`, a wall-clock date and time, zones; `Timestamp` from RFC 3339 text | `std/time` | every database driver's `date` and `timestamp`, HTTP's `Date` field, S3 | `torb/sql` has `Date`, `timestampOf`, `dateOf` and Hinnant's two algorithms, which `std/time` keeps private |
| 12 | **`Decimal` that computes** | `std/number` (milestone 8) | `numeric` (postgres), `DECIMAL` (mysql), `Decimal128` (mongodb) | the digits as text, `SqlValue.Numeric` |
| 13 | **A UUID type** | `std/identifier` (URI.md slice 6, after `std/random`) | `uuid` columns, MongoDB, message identifiers | `torb/sql`'s `Uuid` |
| 14 | **A semaphore for tasks** | `std/task` | every pool, every connection that runs one statement at a time | `torb/sql`'s `Permits`, the design of `std/http`'s private `Slots` |
| 15 | **A generic pool of resources** | `std/task`, once the three that exist are compared | `std/http`'s client, `torb/redis`, `torb/sql` | each has its own |
| 16 | **A buffered reader over a byte stream**: take `n` bytes, a line, a length-prefixed frame, across chunk borders | `std/stream` | every wire protocol | `std/http`'s `Connection`, `torb/postgres`'s `Wire`, `torb/redis`'s transport - three copies |
| 17 | **One transport of TCP or TLS over TCP, upgradable in the middle** | `std/tls` | Postgres's TLS request, MySQL's SSL request, SMTP's `STARTTLS`, `rediss:` | `std/http`'s private `Transport`, `torb/postgres`'s `Wire` |
| 18 | **The peer's certificate** (DER) from a `TlsStream` | `std/tls` | `SCRAM-SHA-256-PLUS` (channel binding `tls-server-end-point`), certificate pinning | the driver speaks SCRAM without channel binding |
| 19 | **Client certificates** (mutual TLS) | `std/tls` | Postgres `sslcert`/`sslkey`, MySQL, MongoDB's X.509 authentication, AMQP `EXTERNAL` | nothing |
| 20 | **ALPN** | `std/tls` | Postgres 17's direct TLS (`sslnegotiation=direct`, ALPN `postgresql`), HTTP/2 and so gRPC | the TLS request, one round trip longer |
| 21 | **Unix domain sockets** | `std/network` | Postgres's and Redis's local sockets, Docker's API | TCP to `localhost` |
| 22 | **Socket options**: `TCP_NODELAY`, keepalive | `std/network` | long-lived database connections: a dead peer is noticed only by keepalive | nothing |
| 23 | **Unicode normalization** (NFKC) | `std/text` | SASLprep (RFC 4013) of a SCRAM password | a password is taken as its UTF-8 bytes |
| 24 | **XML** | `std/xml` (ROADMAP milestone 10, formats) | S3's listings and errors | nothing: `torb/s3` waits |
| 25 | **HTTP/2** | `std/http` (NETWORK.md section 12) | gRPC | nothing |
| 26 | zstd, snappy, lz4 | `std/compression` | MongoDB's wire compression, Kafka | nothing; optional for both |

**What the list says, in short**: the crypto primitives (1 to 6) block the most packages; the wall clock and the
calendar (9 to 11) are needed by every driver and by everything that signs; the stream and transport helpers (14 to
17) are written three times already.

## 9. What building them found in the toolchain

Each with a program that shows it, kept beside the packages until the coordinator files them:

| # | Where | What |
|---|---|---|
| 1 | `torb new` | `torb new --help` makes a project in a directory called `--help`, with `name = "--help"`, instead of printing the help |
| 2 | `std/tls` or its runtime | the first TLS handshake of a process fails - the server `TLS failed`, the client "connection aborted" - where the server's task is held by a top-level `const` of a script; the same lines inside a function work, and so does the top-level form after one handshake inside a function. VM and native. `torb/postgres`'s double of a server runs its TLS conversation inside a function because of it |
| 3 | `std/network` | the second receive of a server socket can answer `Some([])` - an empty chunk, neither bytes nor the end - after the server sent something; `std/tls` loops over it, a reader that takes an empty chunk for data does not |
| 4 | `torb lint` | `redundant-wrap` reports the `Ok` of `return Ok match failure { Some(problem) => Fail problem ... }` in a function answering `Result<Result<T, E>, E>`, and `--fix` removes it: the arm's `Fail` becomes the outer failure. It broke the build here; where the types line up it changes what the function answers |
| 5 | the lowering | interpolating an `Option<ArrayList<UInt8>>` - what `TcpStream.receiveBuffer` answers - fails with "a generic function or a member of a generic type is not supported by the native back end yet (at std/core/src/convert.trb:192:5)", in the VM too; an `Option<List<UInt8>>` prints |
| 6 | the checker | `_` in a closure whose expected type takes a `var` parameter is a `const`: `{ _.append(2) }` is refused where `{ items => items.append(2) }` is accepted |
| 7 | a message | "a top-level `var` read from a function is not supported by the native back end yet" is also what `torb test` in the VM says, for a test body that reads a top-level `var` |

## 10. Slices

| # | Slice | Needs |
|---|---|---|
| 1 | `torb/sql` 0.1.0: values, rows, errors, traits, pool, transactions | **built** |
| 2 | `torb/postgres` 0.1.0 | 1 - **built** |
| 3 | Rows as a `Source<Row>` for an answer too large for memory, and `Pool` checking an idle connection with `ping` after a while | 1 |
| 4 | `torb/postgres`: `COPY` both ways, cancel requests, kept prepared statements, the binary format for the known types | 2 |
| 5 | Publish: the organisation `torb` on packages.torb.dev, trusted publishers (below), `torb/sql` 0.1.0 first, then `torb/postgres` with its `source` line removed | 1, 2 |
| 6 | `torb/mysql` | gap 3; TLS required until gap 6 |
| 7 | `torb/mongodb` | gaps 1 to 3 |
| 8 | `torb/s3` | gaps 1, 9, 24 |
| 9 | `torb/sqlite` | the `foreign` decision |
| 10 | `torb/nats`, `torb/amqp`, `torb/smtp`, `torb/jwt`, `torb/oauth` | gaps 5 to 9 for the ones that sign |

**The trusted publishers each package needs** before its first tag (`docs/contributing/releasing.md` step 7, sent by an
owner of `torb`):

```text
PUT https://packages.torb.dev/api/1/packages/torb/sql/trusted-publishers
{"publishers":[{"repository":"git.torb.dev/torb/sql","workflow":"publish.yml","ref":"refs/tags/v*"},
               {"repository":"TorbScript/sql","workflow":"publish.yml","ref":"refs/tags/v*","environment":"release"}],
 "required":true}

PUT https://packages.torb.dev/api/1/packages/torb/postgres/trusted-publishers
{"publishers":[{"repository":"git.torb.dev/torb/postgres","workflow":"publish.yml","ref":"refs/tags/v*"},
               {"repository":"TorbScript/postgres","workflow":"publish.yml","ref":"refs/tags/v*","environment":"release"}],
 "required":true}
```

The second entry of each is the GitHub mirror, where one exists under that name; a Forgejo publisher names no
environment, because Forgejo's tokens carry none.

## 11. Open

- **A generic `Cache` trait** (URI.md section 11): with `torb/redis` there is one driver; a second (Memcached, or a
  `MemoryCache` that is more than a test double) decides whether the trait goes to `torb/cache` or waits.
- **`SqlValue` without the `clock` capability** is gap 10, a decision about the prelude, not about this package.
- **`sslmode=require`**: libpq's `require` encrypts without checking the certificate, and a program ported from libpq
  may rely on that against a server with a self-signed certificate. This driver checks, because `std/tls` offers no
  way not to; trusting the server's own certificate in `TlsSettings.trusted` is the way through. Whether `std/tls`
  should offer pinning a certificate's hash instead is gap 18's question.
