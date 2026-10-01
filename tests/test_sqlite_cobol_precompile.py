#!/usr/bin/env python3
import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "sqlite_cobol_precompile", ROOT / "tools/sqlite_cobol_precompile.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PrecompilerTest(unittest.TestCase):
    def test_select_into_and_dml(self):
        source = """       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01 DB-ID PIC X(5).
       01 DB-NAME PIC X(20).
           EXEC SQL INCLUDE SQLCA END-EXEC.
       PROCEDURE DIVISION.
           EXEC SQL
             SELECT NAME INTO :DB-NAME FROM PEOPLE WHERE ID=:DB-ID
           END-EXEC.
           EXEC SQL DELETE FROM PEOPLE WHERE ID=:DB-ID END-EXEC.
"""
        output = MODULE.precompile(source)
        self.assertNotIn("EXEC SQL", output)
        self.assertIn("01  SQLX-REQUEST.", output)
        self.assertIn("01  SQLCA.", output)
        self.assertIn("SELECT NAME FROM PEOPLE WHERE ID=?", output)
        self.assertIn("MOVE +100 TO SQLCODE", output)
        self.assertIn("MOVE 5 TO SQLX-BIND-LENGTH (1).", output)
        self.assertEqual(output.count("CALL 'SQLITEX'"), 2)
        self.assertFalse(any(len(line) > 72 for line in output.splitlines()))

    def test_undefined_host_is_rejected(self):
        source = """       DATA DIVISION.
       WORKING-STORAGE SECTION.
           EXEC SQL INCLUDE SQLCA END-EXEC.
       PROCEDURE DIVISION.
           EXEC SQL DELETE FROM PEOPLE WHERE ID=:MISSING END-EXEC.
"""
        with self.assertRaisesRegex(ValueError, "MISSING"):
            MODULE.precompile(source)


if __name__ == "__main__":
    unittest.main()
