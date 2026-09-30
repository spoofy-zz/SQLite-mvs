# Testing SQLite on MVS 3.8j

These commands are run from the project directory on the workstation. They
submit the supplied JCL through the Zowe profile named `hercules`.

## Build and deploy

Build the host-side cc370/libc370 SDK once, then compile, link, and deploy the
`SQLTTEST` load module:

```sh
git submodule update --init --recursive
make sdk
make probe
PATH=/opt/homebrew/bin:$PWD/build/sdk/bin:/usr/bin:/bin \
  python3 mbt/scripts/mbtdeploy.py \
  --project project.toml --builddir build --ld "$PWD/build/sdk/bin/ld370"
```

The deploy target is `IBMUSER.SQLITE.LOAD`.

## Create a clean database

This deletes and recreates the test VSAM clusters, so do not run it when their
contents must be preserved:

```sh
zowe zos-jobs submit local-file jcl/define-rrds.jcl \
  --zosmf-profile hercules --wait-for-output
```

Expected result: `SQLTDEF`, `CC 0000`.

## SQL smoke test

```sh
zowe zos-jobs submit local-file jcl/smoke.jcl \
  --zosmf-profile hercules --wait-for-output
```

The job `SQLTVSAM` must end with `CC 0000`. Its `SYSPRINT` includes the SQLite
version, return code for every SQL phase, page counts before and after
auto-vacuum, and the selected row. A clean first run currently ends with:

```text
grown-pages rc=0
shrink rc=0
insert rc=0
rollback-write rc=0
rollback_rows=0
rollback-check rc=0
page_count=3
shrunk-pages rc=0
id=1 value=MVS RRDS
select rc=0
```

Run the same smoke JCL again without running `define-rrds.jcl`. The selected
row should then have `id=2`, proving persistence across MVS jobs.

## Concurrent-writer locking test

Start the class-A holder without waiting for it to finish:

```sh
zowe zos-jobs submit local-file jcl/lock-holder.jcl \
  --zosmf-profile hercules
```

While it is holding `BEGIN IMMEDIATE`, submit the class-B probe:

```sh
zowe zos-jobs submit local-file jcl/lock-probe.jcl \
  --zosmf-profile hercules --wait-for-output
```

The probe passes when it ends with `CC 0000` and reports:

```text
lock-probe begin rc=5 error=database is locked
lock-probe expected SQLITE_BUSY
```

The holder should also end with `CC 0000` after reporting that it acquired the
lock, held it for 15 seconds, and committed. Classes A and B are intentional:
they allow both jobs to execute in different initiators at the same time.

## Read job output

Replace `JOBnnnnn` with the returned job ID:

```sh
zowe zos-jobs view all-spool-content JOBnnnnn \
  --zosmf-profile hercules
```

Check `JESMSGLG`/`JESYSMSG` for the condition code and `SYSPRINT` for SQLite
output. To inspect the first database RRDS record in hexadecimal, submit:

```sh
zowe zos-jobs submit local-file jcl/print-rrds.jcl \
  --zosmf-profile hercules --wait-for-output
```

## Required MVS allocations

The test program is `SQLTTEST`. At minimum its JCL needs:

```jcl
//STEPLIB DD DISP=SHR,DSN=IBMUSER.SQLITE.LOAD
//SQLDB   DD DISP=SHR,DSN=IBMUSER.SQLITE.RRDS
//SQLJRN  DD DISP=SHR,DSN=IBMUSER.SQLITE.JOURNAL
```
