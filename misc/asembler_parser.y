%define api.pure full
%define api.prefix {asm}
%define parse.error verbose

%code requires {
  #include <string>
  typedef void* yyscan_t;
}

%code {



  int asmlex(ASMSTYPE* value, yyscan_t scanner);
  void asmerror(yyscan_t, const std::string&, const char*) {}
}

%parse-param { yyscan_t scanner }
%parse-param { const std::string& origin }
%lex-param { yyscan_t scanner }

%token WORD DIRECTIVE NUMBER STRING REGISTER CSR
%token DOLLAR COMMA LBRACKET RBRACKET PLUS MINUS LPAREN RPAREN COLON EOL INVALID

%%







input:
    %empty
  | input line
  ;



line:
    EOL
  | label EOL
  | statement EOL
  | label statement EOL
  ;

label:
    WORD COLON
  ;



statement:
    WORD tail
  | DIRECTIVE tail
  ;



tail:
    %empty
  | tail WORD
  | tail DIRECTIVE
  | tail NUMBER
  | tail STRING
  | tail REGISTER
  | tail CSR
  | tail DOLLAR
  | tail COMMA
  | tail LBRACKET
  | tail RBRACKET
  | tail PLUS
  | tail MINUS
  | tail LPAREN
  | tail RPAREN
  ;
%%
