      * SQLite MVS structured COBOL API v2 request block
       01  SQLX-REQUEST.
           05 SQLX-OPERATION       PIC X(8).
           05 SQLX-SQL-LENGTH      PIC S9(4) COMP.
           05 SQLX-SQL             PIC X(2048).
           05 SQLX-BIND-COUNT      PIC S9(4) COMP.
           05 SQLX-BINDS OCCURS 8 TIMES.
              10 SQLX-BIND-TYPE    PIC X.
              10 FILLER            PIC X.
              10 SQLX-BIND-LENGTH  PIC S9(4) COMP.
              10 SQLX-BIND-VALUE   PIC X(64).
           05 SQLX-MAX-ROWS        PIC S9(4) COMP.
           05 SQLX-ROW-COUNT       PIC S9(4) COMP.
           05 SQLX-COLUMN-COUNT    PIC S9(4) COMP.
           05 SQLX-CHANGES         PIC S9(4) COMP.
           05 SQLX-RETURN-CODE     PIC S9(4) COMP.
           05 SQLX-MESSAGE-LENGTH  PIC S9(4) COMP.
           05 SQLX-MESSAGE         PIC X(160).
           05 SQLX-COLUMN-NAME OCCURS 16 TIMES PIC X(32).
           05 SQLX-ROWS OCCURS 10 TIMES.
              10 SQLX-CELLS OCCURS 16 TIMES.
                 15 SQLX-CELL-TYPE   PIC X.
                 15 FILLER            PIC X.
                 15 SQLX-CELL-LENGTH PIC S9(4) COMP.
                 15 SQLX-CELL-VALUE  PIC X(64).
