#include <cctype>
#include <cstdio>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <string.h>
#include <vector>

enum TOKEN_TYPE {
    NON_TERM = -1,   // non-terminal symbol [a-zA-Z_0-9]*
    DERIVE = -2,     // ->
    DISJ = int('|'), // disjunction
    CONJ = int('&'), // conjunction
    NEG = int('~'),  // negation
    SC = int(';'),   // semicolon
    STR_LIT = -99,   // terminal (string literal)
    EOF_TOK = 0,     // end of file
    INVALID = -100   // invalid token
};

struct TOKEN {
    int type = -100;
    std::string lexeme; // actual contents of token
    int lineNo;
    int columnNo;
};

//-------//
// Lexer //
//-------//

FILE *bbnfFile;              // input grammar file
std::string currentStr;      // holds current string being tokenised
static int lineNo, columnNo;

// Create token
static TOKEN returnToken(std::string lexVal, int tokType) {
    TOKEN tok;
    tok.lexeme = lexVal;
    tok.type = tokType;
    tok.lineNo = lineNo;
    tok.columnNo = columnNo - lexVal.length() - 1;
    return tok;
}

// Build token by reading string from file
static TOKEN getToken() {
    char currentChar = ' '; // current character lexer is reading
    char nextChar = ' ';    // character following current character
    currentStr = "";

    // Skip whitespace
    while (isspace(currentChar)) {
        if (currentChar == '\n' || currentChar == '\r') {
            lineNo++;
            columnNo = 1; // new line starts after newline character
        }
        currentChar = fgetc(bbnfFile);
        columnNo++;
    }

    // Build string literal token
    if (currentChar == '"') {
        columnNo++; // discard opening "

        // Add characters to string until closing " reached
        while ((currentChar = fgetc(bbnfFile)) != '"') {

            // \" escape sequence for " to be contained in string literal
            if (currentChar == '\\') {
                nextChar = fgetc(bbnfFile);
                if (nextChar == '"') {
                    currentChar = nextChar; // if \ followed by ", skip \ in currentStr
                    columnNo++;
                }
            }
            currentStr += currentChar;
            columnNo++;
        }

        // Set currentChar to character following closing ", and return string literal token
        currentChar = fgetc(bbnfFile);
        columnNo++;
        return returnToken(currentStr, STR_LIT);
    }

    // Build non-terminal token; symbols can contain letters, digits and underscores
    if (isalnum(currentChar) || (currentChar == '_')) {
        currentStr += currentChar;
        columnNo++;

        // Add characters to string until non-underscore/alphanumeric character reached
        while(isalnum(currentChar = fgetc(bbnfFile)) || currentChar == '_') {
            currentStr += currentChar;
            columnNo++;
        }
        return returnToken(currentStr, NON_TERM);
    }

    if (currentChar == ';') {
        currentChar = fgetc(bbnfFile);
        columnNo++;
        return returnToken(";", SC);
    }

    if (currentChar == '|') {
        currentChar = fgetc(bbnfFile);
        columnNo++;
        return returnToken("|", DISJ);
    }
    
    if (currentChar == '&') {
        currentChar = fgetc(bbnfFile);
        columnNo++;
        return returnToken("&", CONJ);
    }

    if (currentChar == '~') {
        currentChar = fgetc(bbnfFile);
        columnNo++;
        return returnToken("~", NEG);
    }

    // After -, check for > to build -> derivation symbol token
    if (currentChar == '-') {
        nextChar = fgetc(bbnfFile);
        if (nextChar == '>') {
            currentChar = fgetc(bbnfFile);
            columnNo += 2;
            return returnToken("->", DERIVE);
        } else {
            currentChar = nextChar;
            columnNo++;
            return returnToken("-", int('-')); // just return - if no following >
        }
    }

    // Invalid token if none of the previous
    std::string s(1, currentChar);
    currentChar = fgetc(bbnfFile);
    columnNo++;
    return returnToken(s, INVALID);
}

//-----------//
// AST nodes //
//-----------//

// Base class for all AST nodes
class ASTnode {
    public:
        virtual ~ASTnode() {}
        //virtual std::string to_string(int depth) const {return "";}; // for AST printer
};

// Concise aliases
using Node = std::unique_ptr<ASTnode>; // pointer to AST node
using NodeList = std::vector<Node>;    // list of AST nodes

// Node for non-terminal or terminal in conjunct
class SymbNode: public ASTnode {
    bool Terminal;    // true if terminal symbol, false if non-terminal
    std::string Symb; // symbol

    public:
        SymbNode(bool terminal, std::string symb): Terminal(terminal), Symb(symb) {}
        //virtual std::string to_string(int depth) const override;
};

// Conjunct node
class ConjNode: public ASTnode {
    bool Pos;      // true if positive conjunct, false if negative
    NodeList Expr; // symbols making up conjunct

    public:
        ConjNode(bool pos, NodeList expr): Pos(pos), Expr(std::move(expr)) {}
        //virtual std::string to_string(int depth) const override;
};

// Rule node (conjunction of conjuncts)
class RuleNode: public ASTnode {
    TOKEN Nt;           // non-terminal symbol that derives conjunction
    NodeList Conjuncts; // conjuncts making up rule

    public:
        RuleNode(TOKEN nt, NodeList conjuncts): Nt(nt), Conjuncts(std::move(conjuncts)) {}
        //virtual std::string to_string(int depth) const override;
};

// Disjunction of grammar rules for particular non-terminal
class DisjNode: public ASTnode {
    TOKEN Nt;       // non-terminal symbol
    NodeList Rules; // disjunction of rules

    public:
        DisjNode(TOKEN nt, NodeList rules): Nt(nt), Rules(std::move(rules)) {}
        //virtual std::string to_string(int depth) const override;
};

//--------------------------//
// Recursive Descent Parser //
//--------------------------//

static TOKEN CurTok;                 // current token that parser is looking at
static std::deque<TOKEN> tok_buffer; // token buffer

// Read another token from lexer and update current token
static TOKEN getNextToken() {
    if (tok_buffer.size() == 0)
        tok_buffer.push_back(getToken());

    TOKEN temp = tok_buffer.front();
    tok_buffer.pop_front();
    return CurTok = temp;
}

// Display parsing error
// Include token that triggered error, its line and column numbers, and expected sequence
void parseError(std::string expected) {
    std::string report = "Parse error [ln " + std::to_string(CurTok.lineNo) + ", col " + std::to_string(CurTok.columnNo) + "]: unexpected token '" + CurTok.lexeme + "' (expecting " + expected + ")\n";
    std::cout << report;
    exit(1); // quit on finding error
}

// Check if current token is of given type
bool match(TOKEN_TYPE tokType) {
    if (CurTok.type == tokType) {
        getNextToken(); // if matched, move on to next token
        return true;
    }
    return false; // do not move on, current token will likely be checked again
}

// symbol ::= NON_TERM | '"' STR_LIT '"'
static Node parseSymbol() {
    bool terminal = true;
    if (CurTok.type == NON_TERM)
        terminal = false;
    else if (CurTok.type != STR_LIT)
        parseError("non-terminal or literal"); // error if neither NON_TERM nor STR_LIT

    std::string symb = CurTok.lexeme; // get symbol
    getNextToken();                   // move on to next token
    return std::make_unique<SymbNode>(terminal, symb);
}

// clist ::= '&' conjunct clist
//        |  epsilon
// conjunct ::= neg symbol slist
// neg ::= '~' | epsilon
// slist ::= symbol slist
//        |  epsilon
static Node parseConj() {
    bool pos = true;   // assume positive conjunct
    if (match(NEG))
        pos = false;   // negative conjunct if preceded by '~'

    // Add symbol to list until '&' or semicolon reached
    NodeList symbList;
    do {
        symbList.push_back(parseSymbol());
    } while (!match(CONJ) && !match(SC));
    return std::make_unique<ConjNode>(pos, std::move(symbList));
}

// rlist ::= '|' rule rlist
//        |  epsilon
// rule ::= conjunct clist
static Node parseRule(TOKEN nt) {
    // Add conjunct to list until '|' or semicolon reached
    NodeList conjList;
    do {
        conjList.push_back(parseConj());
    } while (!match(DISJ) && !match(SC));
    return std::make_unique<RuleNode>(nt, std::move(conjList));
}

// disjunction ::= NON_TERM '->' rule rlist ';'
static Node parseDisj() {
    // If current token is a non-terminal, move on to next token; if not, error
    TOKEN nt = CurTok;
    if (nt.type == NON_TERM)
        getNextToken();
    else
        parseError("non-terminal");

    // Syntax error if non-terminal not followed by '->'
    if (!match(DERIVE))
        parseError("'->'");

    // Add rule to list until semicolon reached
    NodeList ruleList;
    do {
        ruleList.push_back(parseRule(nt));
    } while (!match(SC));
    return std::make_unique<DisjNode>(nt, std::move(ruleList));
}

// grammar ::= disjunction dlist
// dlist ::= disjunction dlist
//        |  epsilon
static NodeList parseGrammar() {
    getNextToken();    // get first token
    NodeList disjList; // initialise disjunction list

    // Add disjunction to list until EOF reached
    do {
        disjList.push_back(parseDisj());
    } while (!match(EOF_TOK));
    return disjList;
}
