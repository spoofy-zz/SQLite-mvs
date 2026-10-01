      * SQLite embedded-SQL compatibility area
       01  SQLCA.
           05 SQLCODE             PIC S9(4) COMP VALUE +0.
           05 SQLROWC             PIC S9(4) COMP VALUE +0.
           05 SQLCHNG             PIC S9(4) COMP VALUE +0.
           05 SQLERRM             PIC X(160) VALUE SPACES.
