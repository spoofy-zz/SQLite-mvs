         TITLE 'TSQTGET - SQLITE TSO LINE INPUT'
*
* C-callable: int tsqtget(char *buf, int max)
*
TSQTGET  CSECT
         STM   14,12,12(13)
         LR    12,15
         USING TSQTGET,12
         LR    11,1
         L     2,0(11)
         L     3,4(11)
         LTR   3,3
         BNP   BAD
         BCTR  3,0
         LTR   3,3
         BNP   BAD
         TGET  (2),(3),ASIS,WAIT
         LTR   15,15
         BNZ   BAD
         LR    15,1
         B     DONE
BAD      L     15,=F'-1'
DONE     L     14,12(13)
         LM    0,12,20(13)
         BR    14
         LTORG
         END   TSQTGET
