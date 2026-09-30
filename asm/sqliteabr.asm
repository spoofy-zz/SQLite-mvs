         TITLE 'COBOL BRIDGE TO SQLITEA RUNTIME'
SQLITEA  CSECT
         STM   14,12,12(13)
         LR    12,15
         USING SQLITEA,12
         LR    11,1
         LA    10,SAVEAREA
         ST    13,4(10)
         ST    10,8(13)
         LR    13,10
         LOAD  EP=SQLITEA,ERRET=LOADERR
         LR    15,0
         LR    1,11
         BALR  14,15
         ST    15,RESULT
         DELETE EP=SQLITEA
         B     RETURN
LOADERR  LA    15,16
         ST    15,RESULT
         L     2,20(11)
         N     2,=X'00FFFFFF'
         STH   15,0(2)
         L     2,16(11)
         SR    0,0
         STH   0,0(2)
RETURN   L     13,4(13)
         L     15,RESULT
         L     14,12(13)
         LM    0,12,20(13)
         BR    14
         DS    0F
SAVEAREA DC    18F'0'
RESULT   DC    F'0'
         LTORG
         DROP  12
         TITLE 'COBOL BRIDGE TO SQLITEX STRUCTURED RUNTIME'
SQLITEX  CSECT
         STM   14,12,12(13)
         LR    12,15
         USING SQLITEX,12
         LR    11,1
         LA    10,XSAVE
         ST    13,4(10)
         ST    10,8(13)
         LR    13,10
         LOAD  EP=SQLITEX,ERRET=XLOADERR
         LR    15,0
         LR    1,11
         BALR  14,15
         ST    15,XRESULT
         DELETE EP=SQLITEX
         B     XRETURN
XLOADERR LA    15,16
         ST    15,XRESULT
         L     2,0(11)
         N     2,=X'00FFFFFF'
         STH   15,2612(2)
XRETURN  L     13,4(13)
         L     15,XRESULT
         L     14,12(13)
         LM    0,12,20(13)
         BR    14
         DS    0F
XSAVE    DC    18F'0'
XRESULT  DC    F'0'
         LTORG
         END   SQLITEA
