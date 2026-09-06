%option reentrant bison-bridge noyywrap nodefault noinput nounput
%option prefix="asm"
%option header-file="build/asembler_lexer.hpp"






%x AFTER_END DISCARD

%top{
#include "asembler_parser.hpp"
#define YYSTYPE ASMSTYPE
}

%%

[ \t]+                              ;
\#[^\r\n]*                          ;
\r?\n                               return EOL;



\.end                               BEGIN(AFTER_END); return DIRECTIVE;



\.[A-Za-z_][A-Za-z0-9_.]*          return DIRECTIVE;
%r[0-9]+|%sp|%pc                    return REGISTER;
%status|%handler|%cause             return CSR;
0[xX][0-9A-Fa-f]+|[0-9]+           return NUMBER;
\"([^\"\\\r\n]|\\.)*\"              return STRING;
[A-Za-z_.][A-Za-z0-9_.]*           return WORD;
"$"                                 return DOLLAR;
","                                 return COMMA;
"["                                 return LBRACKET;
"]"                                 return RBRACKET;
"+"                                 return PLUS;
"-"                                 return MINUS;
"("                                 return LPAREN;
")"                                 return RPAREN;
":"                                 return COLON;
.                                   return INVALID;



<AFTER_END>[ \t]+                   ;
<AFTER_END>\#[^\r\n]*               ;
<AFTER_END>\r?\n                    BEGIN(DISCARD); return EOL;
<AFTER_END>.                        return INVALID;


<DISCARD>(.|\n)                     ;
<DISCARD><<EOF>>                    return 0;
%%
