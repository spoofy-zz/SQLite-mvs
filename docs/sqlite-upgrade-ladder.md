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

These are host-side toolchain results, not yet runtime certification of every
version on MVS. cc370 emits pointer/integer-size warnings in upstream SQLite
allocator and B-tree code, but all five versions link without patches to
`sqlite3.c`.

The 3.37.2 runtime suite passed on MVS 3.8j as `SQLT372T JOB01334` with
`RC=0000`. Its spool is retained locally at
`build/upgrades/3.37.2/suite.spool` (an ignored build artifact).

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
