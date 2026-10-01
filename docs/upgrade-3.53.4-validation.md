# SQLite 3.53.4 full-stack validation

SQLite 3.53.4 is the active deployment in `IBMUSER.SQLITE.D534.LOAD`. The
standard `SQLITE` command and versioned `SQL534` alias both start this stack.
The former 3.8.11.1 `IBMUSER.SQLITE.LOAD` deployment has been retired.

## Validated components

| Component | MVS result |
|---|---|
| SQL regression and modern syntax | JOB01341, CC 0000 |
| Text COBOL API (`SQLITEA`) | JOB01343, runtime CC 0000 |
| Structured COBOL API (`SQLITEX`) | JOB01344, runtime CC 0000 |
| KICKS people/orders screens | JOB01345, CC 0000 |
| RRDS smoke, growth, rollback | JOB01346, CC 0000 |
| Intentional crash | JOB01347, expected CC 0012 |
| Hot-journal recovery | JOB01348, CC 0000, `crash_rows=0` |
| Backup, restore, ATTACH | JOB01350, CC 0000 |
| Lock holder | JOB01357, CC 0000 |
| Busy-timeout waiter | JOB01358, CC 0000, waited 10 seconds |

The KICKS batch scenario exercised initial search, both result pages, the
last-page boundary, previous-page navigation, clear, person orders, and the
detail screen. It used the existing compiled KICKS programs and COBOL bridge;
only STEPLIB was changed to select the 3.53.4 `SQLITEX` implementation.

## Interactive commands

Use `SQLITE` (or its `SQL534` alias) for the SQLite 3.53.4 TSO shell. Use
`SQLKICKS` to start the existing KICKS application with the 3.53.4 API. Within KICKS, enter `SQLS`.
The command is installed as `SYS2.CMDPROC(SQLKICKS)`.

The candidate deploy replaces the complete `D534.LOAD` PDS. After every
redeploy, submit `kicks/sqlite-search/STARTUP-3.53.4.jcl` to copy `KIKSIP1$`
back into that PDS before starting `SQLKICKS`.

Active datasets are:

- `IBMUSER.SQLITE.D534.LOAD`
- `IBMUSER.SQLITE.D534DB` and `D534JRN`
- `IBMUSER.SQLITE.D534BAK` and `D534BJR`

KICKS intentionally uses the existing sample `TESTDB` and `TESTJRN`, so the
same people/orders data and application behavior can be compared directly
with the legacy deployment.
