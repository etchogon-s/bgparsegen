#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
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

// Non-terminal or terminal in conjunct
struct SYMBOL {
    bool terminal;    // true if terminal, false if non-terminal
    std::string symb;
};

using SymbList = std::vector<SYMBOL>; // list of symbols

// Conjunct (list of symbols)
class Conjunct {
    bool Pos;      // true if positive conjunct, false if negative
    SymbList Expr; // symbols making up conjunct

    public:
        Conjunct(bool pos, SymbList expr): Pos(pos), Expr(std::move(expr)) {}
};

using Conj = std::unique_ptr<Conjunct>; // pointer to conjunct
using ConjList = std::vector<Conj>;     // list of conjuncts

std::set<std::string> alphabet;                     // set of terminal symbols
std::map<std::string, std::set<ConjList>> nonTerms; // set of non-terminals and rules

// symbol ::= NON_TERM | '"' STR_LIT '"'
static SYMBOL parseSymbol() {
    bool terminal = true;
    if (CurTok.type == NON_TERM)
        terminal = false;
    else if (CurTok.type != STR_LIT)
        parseError("non-terminal or literal"); // error if neither NON_TERM nor STR_LIT

    // Get symbol and add to alphabet if terminal
    std::string symb = CurTok.lexeme;
    if (terminal)
        alphabet.insert(symb);

    getNextToken(); // move on to next token

    // Create & return new symbol
    SYMBOL newSymb;
    newSymb.terminal = terminal;
    newSymb.symb = symb;
    return newSymb;
}

// clist ::= '&' conjunct clist
//        |  epsilon
// conjunct ::= neg symbol slist
// neg ::= '~' | epsilon
// slist ::= symbol slist
//        |  epsilon
static Conj parseConj() {
    bool pos = true; // assume positive conjunct
    if (match(NEG))
        pos = false; // negative conjunct if preceded by '~'

    // Add symbol to list until '&' or semicolon reached
    SymbList symbols;
    do {
        symbols.push_back(parseSymbol());
    } while (!match(CONJ) && !match(SC));
    return std::make_unique<Conjunct>(pos, std::move(symbols));
}

// rlist ::= '|' rule rlist
//        |  epsilon
// rule ::= conjunct clist
static ConjList parseRule() {
    // Add conjunct to set until '|' or semicolon reached
    ConjList cList;
    do {
        cList.push_back(parseConj());
    } while (!match(DISJ) && !match(SC));
    return cList;
}

// disjunction ::= NON_TERM '->' rule rlist ';'
static void parseDisj() {
    // If current token is a non-terminal, move on to next token; if not, error
    TOKEN nt = CurTok;
    if (nt.type == NON_TERM)
        getNextToken();
    else
        parseError("non-terminal");

    // Syntax error if non-terminal not followed by '->'
    if (!match(DERIVE))
        parseError("'->'");

    // Add rule (set of conjuncts) to set until semicolon reached
    std::set<ConjList> rules;
    do {
        rules.insert(parseRule());
    } while (!match(SC));

    // If nt is already a key in the map, merge its existing rule set with new rule set
    std::string ntSymb = nt.lexeme;
    if (nonTerms.count(ntSymb))
        nonTerms.at(ntSymb).merge(rules);
    else
        nonTerms[ntSymb] = std::move(rules);
}

// grammar ::= disjunction dlist
// dlist ::= disjunction dlist
//        |  epsilon
static void parseGrammar() {
    getNextToken(); // get first token

    // Add disjunction to list until EOF reached
    do {
        parseDisj();
    } while (!match(EOF_TOK));
}

//-------------//
// Main Driver //
//-------------//

// Get input file and user's algorithm choice
int main(int argc, char **argv) {
    if (argc == 3) {
        bbnfFile = fopen(argv[1], "r");
        if (bbnfFile == NULL)
            std::cout << "Error opening file\n";
    } else {
        std::cout << "Usage: ./code <input file> <algorithm>\n";
        return 1;
    }

    // Initialise line and column numbers
    lineNo = 1;
    columnNo = 1;

    // Run parser
    parseGrammar();
    std::cout << "Parsing finished\n";

    fclose(bbnfFile);
    return 0;
}
