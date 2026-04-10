         keep  obj/orca
         mcopy orca.macros
         case  on

****************************************************************
*
*  ORCA - ORCA/C specific libraries
*
*  This code implements the tables and subroutines needed to
*  support the ORCA/C library ORCA.
*
*  March 1989
*  Mike Westerfield
*
*  Copyright 1989
*  Byte Works, Inc.
*
****************************************************************
*
ORCA     start                          dummy segment
         end

****************************************************************
*
*  char *commandline(void)
*
*  Inputs:
*        ~CommandLine - address of the command line
*
****************************************************************
*
commandline start

         ldx   #0
         lda   ~COMMANDLINE
         ora   ~COMMANDLINE+2
         beq   lb1

         lda   ~COMMANDLINE
         ldx   ~COMMANDLINE+2
         clc
         adc   #8
         bcc   lb1
         inx
lb1      rtl
         end

****************************************************************
*
*  void enddesk(void)
*
****************************************************************
*
enddesk  start

         brl   ~ENDDESK
         end

****************************************************************
*
*  void endgraph(void)
*
****************************************************************
*
endgraph start

         brl   ~ENDGRAPH
         end

****************************************************************
*
*  char *shellid(void)
*
*  Inputs:
*        ~CommandLine - address of the command line
*
****************************************************************
*
shellid  start

         ldx   #0                       return NULL if there is no command line
         lda   >~COMMANDLINE
         ora   >~COMMANDLINE+2
         bne   lb1
         rtl

lb1      lda   >~COMMANDLINE+2
         pha
         lda   >~COMMANDLINE
         pha
         phd
         tsc
         tcd
         phb
         phk
         plb
         ldy   #6
lb2      lda   [3],Y
         sta   id,Y
         dey
         dey
         bpl   lb2
         plb
         pld
         pla
         pla
         lda   #id
         ldx   #^id
         rtl

id       dc    8c' ',i1'0'
         end

****************************************************************
*
*  void startdesk(int width)
*
****************************************************************
*
startdesk start

         brl   ~STARTDESK
         end

****************************************************************
*
*  void startgraph(int width)
*
****************************************************************
*
startgraph start

         brl   ~STARTGRAPH
         end

****************************************************************
*
*  int toolerror(void)
*
****************************************************************
*
toolerror start

         lda   >~TOOLERROR
         rtl
         end

****************************************************************
*
*  int userid(void)
*
****************************************************************
*
userid   start

         lda   >~USER_ID
         rtl
         end

****************************************************************
*
*  char *getVersionString(unsigned int userID)
*
*  Returns a pointer to the null-terminated version string from
*  the ~Version segment of the binary identified by userID, or
*  NULL if no ~Version segment is found.
*
*  Uses Loader calls GetPathname ($1011) and LoadSegName ($0D11).
*
****************************************************************
*
getVersionString start
result   equ   1

         csubroutine (2:uID),4

         phb
         phk
         plb

* GetPathname(uID) — returns pointer to class 1 pathname
         pha                         result space (long)
         pha
         ph2   <uID
         ldx   #$1011                GetPathname
         jsl   $E10000
         sta   >~TOOLERROR
         bcs   err1

         pla
         sta   pathPtr
         pla
         sta   pathPtr+2

* LoadSegName(uID, pathname, "~Version  ") — returns segAddr + info
         tsc                         reserve 10 bytes result space
         sec
         sbc   #10
         tcs
         ph2   <uID
         lda   pathPtr+2
         pha
         lda   pathPtr
         pha
         ph4   #vName
         ldx   #$0D11                LoadSegName
         jsl   $E10000
         sta   >~TOOLERROR
         bcs   err2

         pla                         segAddr (low)
         sta   result
         pla                         segAddr (high)
         sta   result+2
         pla                         discard userID
         pla                         discard fileNum
         pla                         discard segNum
         bra   done

err1     pla                         discard GetPathname result space
         pla
         bra   null

err2     tsc                         discard LoadSegName result space
         clc
         adc   #10
         tcs

null     stz   result
         stz   result+2

done     plb
         creturn 4:result
;
;  Local data
;
pathPtr  ds    4
vName    dc    c'~Version  '         10-char blank-padded segment name
         end
