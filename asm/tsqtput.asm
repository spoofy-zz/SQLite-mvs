         TITLE 'TSQTPUT - SQLITE TSO LINE OUTPUT'
*
* C-callable: int tsqtput(char *buf, int len)
*
TSQTPUT  CSECT
         STM   14,12,12(13)
         LR    12,15
         USING TSQTPUT,12
         LR    11,1
         L     2,0(11)
         L     3,4(11)
         LTR   3,3
         BNP   DONEOK
         TPUT  (2),(3),EDIT,WAIT
         LTR   15,15
         BNZ   DONE
DONEOK   SR    15,15
DONE     L     14,12(13)
         LM    0,12,20(13)
         BR    14
         LTORG

* C-callable: int tsqtrecl(char *buf, int len)
* Write a caller-built 3270 data stream without holding the keyboard.
         DROP 12
TSQTRECL CSECT
         STM   14,12,12(13)
         LR    12,15
         USING TSQTRECL,12
         LR    11,1
         L     2,0(11)
         L     3,4(11)
         LTR   3,3
         BNP   RECBAD
         TPUT  (2),(3),FULLSCR,,NOHOLD
         LTR   15,15
         BNZ   RECDONE
         SR    15,15
         B     RECDONE
RECBAD   L     15,=F'-1'
RECDONE  L     14,12(13)
         LM    0,12,20(13)
         BR    14
         LTORG

* C-callable: int tsqtline(void)
* Return from full-screen mode after the recalled field has been read.
         DROP 12
TSQTLINE CSECT
         STM   14,12,12(13)
         LR    12,15
         USING TSQTLINE,12
         LA    1,1
         LA    0,19
         SLL   0,24
         SVC   94
         SR    15,15
         L     14,12(13)
         LM    0,12,20(13)
         BR    14
         LTORG

* C-callable: int tsqtclr(void)
* Erase the complete 3270 display, restore line mode, and make the next
* TPUT EDIT start at row 1. CLSCR is WCC restore-keyboard/reset-MDT,
* SBA home, RA home with an EBCDIC blank, then IC at home.
         DROP 12
TSQTCLR  CSECT
         STM   14,12,12(13)
         LR    12,15
         USING TSQTCLR,12
         LA    2,CLSCR
         LA    3,L'CLSCR
         TPUT  (2),(3),FULLSCR,,NOHOLD
         LTR   15,15
         BNZ   CLRDONE
         LA    1,1
         LA    0,19
         SLL   0,24
         SVC   94
         SR    15,15
CLRDONE  L     14,12(13)
         LM    0,12,20(13)
         BR    14
CLSCR    DC    X'C11140403C40404013'
         LTORG
         END   TSQTPUT
