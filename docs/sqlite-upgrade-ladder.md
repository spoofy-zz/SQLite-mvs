# SQLite upgrade ladder on MVS 3.8j

Development of the upgrade path is isolated on the `devel` branch. The
stable `main` branch and its `SQLTTEST` module remain unchanged.

## Reproducible compiler probes

Run one complete compiler, assembler, and linker probe with:

```sh
tools/probe_sqlite_upgrade.sh 3.37.2 full
```

Run the complete ladder with:

```sh
make upgrade-matrix
```

The script downloads the unmodified official SQLite amalgamation into the
ignored `build/upgrades/VERSION/` directory. It then generates a matching
cc370 short-symbol header and builds the amalgamation together with the MVS
VFS and regression program. Each version receives its own load-module name.

| SQLite | Module | cc370 C | as370 | ld370 | MVS runtime |
|---|---|---:|---:|---:|---:|
| 3.15.2 | `SQLT315` | pass | pass | pass | not run |
| 3.22.0 | `SQLT322` | pass | pass | pass | not run |
| 3.31.1 | `SQLT331` | pass | pass | pass | not run |
| 3.35.5 | `SQLT355` | pass | pass | pass | not run |
| 3.37.2 | `SQLT372` | pass | pass | pass | pass |
| 3.40.1 | `SQLT401` | pass | pass | pass | not run |
| 3.45.3 | `SQLT453` | pass | pass | pass | not run |
| 3.49.2 | `SQLT492` | pass | pass | pass | not run |
| 3.53.4 | `SQLT534` | pass | pass | pass | pass |

These are host-side toolchain results, not yet runtime certification of every
version on MVS. cc370 emits pointer/integer-size warnings in upstream SQLite
allocator and B-tree code, but all five versions link without patches to
`sqlite3.c`.

The 3.37.2 runtime suite passed on MVS 3.8j as `SQLT372T JOB01334` with
`RC=0000`. Its spool is retained locally at
`build/upgrades/3.37.2/suite.spool` (an ignored build artifact).

The current upstream release, 3.53.4, also passed on MVS 3.8j as
`SQLT534T JOB01341` with `RC=0000` after deploying both the batch and TSO
modules. In addition to the common suite, it ran
UPSERT, window functions, generated columns, `RETURNING`, strict tables,
JSON, RIGHT JOIN, JSONB, numeric underscores, two-argument and variadic
`iif()`, `unistr()`, and `json_array_insert()`.

SQLite 3.53 introduced use of the C99 `INFINITY` macro, which libc370 does
not provide. `sqlite3_mvs_compat.h` maps it to libc370's `HUGE_VAL` overflow
sentinel. Upstream `sqlite3.c` remains unmodified. Since MVS/370 hexadecimal
floating point has no IEEE infinity, extreme decimal overflow behavior is a
known compatibility difference and needs dedicated numeric testing.

## Isolated 3.37.2 MVS test

Build `SQLT372`, deploy it to its candidate-only load library, define isolated
RRDS files, and run the suite:

```sh
tools/probe_sqlite_upgrade.sh 3.37.2 full
PATH="$PWD/build/sdk/bin:$PATH" python3 mbt/scripts/mbtdeploy.py \
  --project upgrade/project-3.37.2.toml \
  --builddir build/upgrades/3.37.2/probe \
  --ld "$PWD/build/sdk/bin/ld370"
python3 tools/submit_mvs_jcl.py jcl/define-upgrade-3.37.2.jcl
python3 tools/submit_mvs_jcl.py jcl/upgrade-3.37.2-suite.jcl \
  --spool build/upgrades/3.37.2/suite.spool
```

Use this runner instead of a separately configured Zowe profile: it reads the
same `.env`/MBT connection as deployment, preventing jobs from being submitted
to a different MVS instance.

The candidate uses `IBMUSER.SQLITE.D372.LOAD`, `D372DB`, and `D372JRN`; it
does not overwrite the stable load library or database. The 3.37.2 suite adds
runtime checks for UPSERT, window functions, generated columns, `RETURNING`,
and `STRICT` tables in addition to the common transaction, constraint, view,
foreign-key, BLOB, and integrity tests.

## Interactive TSO candidate

Build the separate `SQLI372` shell and redeploy both candidate modules:

```sh
tools/probe_sqlite_upgrade.sh 3.37.2 tso
PATH="$PWD/build/sdk/bin:$PATH" python3 mbt/scripts/mbtdeploy.py \
  --project upgrade/project-3.37.2.toml \
  --builddir build/upgrades/3.37.2/probe \
  --ld "$PWD/build/sdk/bin/ld370"
```

Install `clist/SQL372.clist` as a command member such as
`SYS2.CMDPROC(SQL372)`. From TSO READY, start the isolated shell with:

```text
SQL372
```

Verify it interactively with `.version`, `.databases`, an UPSERT or window
query, and `.quit`. The CLIST allocates only `D372DB` and `D372JRN` and calls
`SQLI372`; the stable `SQLITE` command remains unchanged.

The same isolated setup is available for the current 3.53.4 release as
`SQLI534`, `IBMUSER.SQLITE.D534.LOAD`, `D534DB`, and `D534JRN`. Install
`clist/SQL534.clist` as `SYS2.CMDPROC(SQL534)` and start it with `SQL534`.
