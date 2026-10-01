       IDENTIFICATION DIVISION.
       PROGRAM-ID. SQLETST.
       ENVIRONMENT DIVISION.
       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  DB-ID                   PIC X(5) VALUE '99'.
       01  DB-NAME                 PIC X(20) VALUE SPACES.
       01  DB-CITY                 PIC X(20) VALUE SPACES.
       01  DB-AGE                  PIC X(3) VALUE SPACES.
       01  TEST-STATUS             PIC S9(4) COMP VALUE +0.
           EXEC SQL INCLUDE SQLCA END-EXEC.
       PROCEDURE DIVISION.
           EXEC SQL
               DELETE FROM PEOPLE WHERE ID = :DB-ID
           END-EXEC.

           MOVE 'EXEC SQL' TO DB-NAME.
           MOVE 'ZAGREB' TO DB-CITY.
           MOVE '45' TO DB-AGE.
           EXEC SQL
               INSERT INTO PEOPLE(ID, NAME, CITY, AGE)
               VALUES(:DB-ID, RTRIM(:DB-NAME),
                      RTRIM(:DB-CITY), :DB-AGE)
           END-EXEC.
           DISPLAY 'INSERT SQLCODE=' SQLCODE.
           IF SQLCODE NOT = 0 MOVE 12 TO TEST-STATUS.

           MOVE SPACES TO DB-NAME DB-CITY DB-AGE.
           EXEC SQL
               SELECT NAME, CITY, AGE
                 INTO :DB-NAME, :DB-CITY, :DB-AGE
                 FROM PEOPLE WHERE ID = :DB-ID
           END-EXEC.
           DISPLAY 'SELECT SQLCODE=' SQLCODE ' NAME=' DB-NAME.
           IF SQLCODE NOT = 0 MOVE 12 TO TEST-STATUS.
           IF DB-NAME NOT = 'EXEC SQL' MOVE 12 TO TEST-STATUS.

           MOVE 'UPDATED' TO DB-NAME.
           EXEC SQL
               UPDATE PEOPLE SET NAME = RTRIM(:DB-NAME)
                WHERE ID = :DB-ID
           END-EXEC.
           DISPLAY 'UPDATE SQLCODE=' SQLCODE.
           IF SQLCODE NOT = 0 MOVE 12 TO TEST-STATUS.

           MOVE SPACES TO DB-NAME.
           EXEC SQL
               SELECT NAME INTO :DB-NAME
                 FROM PEOPLE WHERE ID = :DB-ID
           END-EXEC.
           IF DB-NAME NOT = 'UPDATED' MOVE 12 TO TEST-STATUS.

           EXEC SQL
               DELETE FROM PEOPLE WHERE ID = :DB-ID
           END-EXEC.
           DISPLAY 'DELETE SQLCODE=' SQLCODE.
           IF SQLCODE NOT = 0 MOVE 12 TO TEST-STATUS.

           EXEC SQL
               SELECT NAME INTO :DB-NAME
                 FROM PEOPLE WHERE ID = :DB-ID
           END-EXEC.
           DISPLAY 'NOT FOUND SQLCODE=' SQLCODE.
           IF SQLCODE NOT = +100 MOVE 12 TO TEST-STATUS.
           IF TEST-STATUS = 0
               DISPLAY 'EXEC SQL API TEST PASSED'
           ELSE
               DISPLAY 'EXEC SQL API TEST FAILED'.
           MOVE TEST-STATUS TO RETURN-CODE.
           STOP RUN.
