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
         END   TSQTPUT
