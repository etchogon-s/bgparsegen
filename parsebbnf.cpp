#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

//-------//
// Lexer //
//-------//

enum TOKEN_TYPE {
    NON_TERM, // non-terminal symbol
    DERIVE,   // '->' (derivation)
    DISJ,     // '|' (disjunction)
    CONJ,     // '&' (conjunction)
    NEG,      // '~' (negation)
    SC,       // semicolon
    STR_LIT,  // terminal (string literal)
    EPSILON,  // written in BBNF as 'epsilon'
    EOF_TOK,  // end of file
    INVALID,  // invalid token
};

struct TOKEN {
    int type;
    std::string lexeme; // actual contents of token
    int lineNo;
    int columnNo;
};

FILE *bbnfFile;              // input grammar file
char currentChar = ' ';      // current character lexer is reading
char nextChar = ' ';         // character following current character
static int lineNo, columnNo;

// Create token
static TOKEN returnToken(std::string lexVal, int tokType) {
    TOKEN tok;
    tok.lexeme = lexVal;
    tok.type = tokType;
    tok.lineNo = lineNo;
    tok.columnNo = columnNo - lexVal.length() - 1;
    std::cout << "Found token " + lexVal + " type " + std::to_string(tokType) + "\n";
    return tok;
}

// Build token by reading characters from file
static TOKEN getToken() {
    std::string currentStr = ""; // holds current string being tokenised

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

        if (currentStr == "epsilon")
            return returnToken(currentStr, EPSILON);
        return returnToken(currentStr, NON_TERM);
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
            return returnToken("-", INVALID); // just return - if no following >
        }
    }

    // Build single character tokens, if character not recognised create invalid token
    TOKEN newTok;
    switch (currentChar) {
        case '|':
            newTok = returnToken("|", DISJ);
            break;
        case '&':
            newTok = returnToken("&", CONJ);
            break;
        case '~':
            newTok = returnToken("~", NEG);
            break;
        case ';':
            newTok = returnToken(";", SC);
            break;
        case EOF:
            return returnToken("EOF", EOF_TOK); // end of file reached, do not read more characters
            break;
        default:
            std::string s(1, currentChar);
            newTok = returnToken(s, INVALID);
            break;
    }

    currentChar = fgetc(bbnfFile);
    columnNo++;
    return newTok;
}

//-------------------//
// Grammar AST Nodes //
//-------------------//

// Non-terminal or terminal in conjunct
struct SYMBOL {
    bool terminal;   // true if terminal, false if non-terminal
    std::string str; // actual symbol
};

using SymbVec = std::vector<SYMBOL>;
using StrSet = std::set<std::string>;
using StrVec = std::vector<std::string>;

class ASTNode {
    public:
        virtual ~ASTNode() {}
        virtual std::string toString(int depth) const {return "";};
        virtual StrSet references() const {return StrSet();};
        virtual StrSet firstSet() const {return StrSet();};
};

using Node = std::unique_ptr<ASTNode>;
using NodeList = std::vector<Node>;

// Conjunct (list of symbols)
class Conjunct: public ASTNode {
    bool Pos;             // true if positive conjunct, false if negative
    SymbVec Symbols;      // symbols making up conjunct
    StrSet NtsReferenced; // set of non-terminals used in conjunct

    public:
        Conjunct(bool pos, SymbVec symbols, StrSet ntsReferenced): Pos(pos), Symbols(std::move(symbols)), NtsReferenced(ntsReferenced) {}
        virtual std::string toString(int depth) const override;
        virtual StrSet references() const override;
        virtual StrSet firstSet() const override;
};

// Rule (list of conjuncts)
class Rule: public ASTNode {
    NodeList ConjList;

    public:
        Rule(NodeList conjList): ConjList(std::move(conjList)) {}
        virtual std::string toString(int depth) const override;
        virtual StrSet references() const override;
        virtual StrSet firstSet() const override;
};

// Disjunction (list of rules derived by a non-terminal)
class Disj: public ASTNode {
    NodeList RuleList;

    public:
        Disj(NodeList ruleList): RuleList(std::move(ruleList)) {}
        virtual std::string toString(int depth) const override;
        virtual StrSet references() const override;
        virtual StrSet firstSet() const override;
};

//--------------------------//
// Recursive Descent Parser //
//--------------------------//

static TOKEN CurTok;                // current token that parser is looking at
static std::deque<TOKEN> tokBuffer; // token buffer

// Read another token from lexer and update current token
static TOKEN getNextToken() {
    if (tokBuffer.size() == 0)
        tokBuffer.push_back(getToken());

    TOKEN temp = tokBuffer.front();
    tokBuffer.pop_front();
    return CurTok = temp;
}

/* Display parsing error
 * Include token that triggered error, line and column numbers, and expected sequence */
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

StrSet alphabet; // set of terminal symbols

// symbol ::= NON_TERM | '"' STR_LIT '"' | 'epsilon'
static SYMBOL parseSymbol() {
    std::cout << "Parsing symbol\n";
    bool isTerminal = true;
    if (CurTok.type == NON_TERM)
        isTerminal = false;
    else if ((CurTok.type != STR_LIT) && (CurTok.type != EPSILON))
        parseError("non-terminal or literal"); // error if not NON_TERM, STR_LIT or EPSILON

    // Get symbol and add to alphabet if terminal
    std::string symbStr = CurTok.lexeme;
    if (isTerminal)
        alphabet.insert(symbStr);

    getNextToken(); // move on to next token

    // Create & return new symbol
    SYMBOL symb;
    symb.terminal = isTerminal;
    symb.str = symbStr;
    return symb;
}

// clist ::= '&' conjunct clist
//        |  epsilon
// conjunct ::= neg symbol slist
// neg ::= '~' | epsilon
// slist ::= symbol slist
//        |  epsilon
static Node parseConj() {
    std::cout << "Parsing conjunct\n";
    bool pos = true; // assume positive conjunct
    if (match(NEG))
        pos = false; // negative conjunct if preceded by '~'

    /* Add symbol to list until ampersand, pipe or semicolon reached
     * If symbol is non-terminal, add to set of non-terminals */
    SymbVec symbols;
    StrSet ntsReferenced;
    do {
        SYMBOL nextSymb = parseSymbol();
        symbols.push_back(nextSymb);
        if (!nextSymb.terminal)
            ntsReferenced.insert(nextSymb.str);
    } while ((CurTok.type != CONJ) && (CurTok.type != DISJ) && (CurTok.type != SC));
    return std::make_unique<Conjunct>(pos, std::move(symbols), ntsReferenced);
}

// rlist ::= '|' rule rlist
//        |  epsilon
// rule ::= conjunct clist
static Node parseRule() {
    std::cout << "Parsing rule\n";
    NodeList conjList;

    /* Ampersand follows every conjunct except last conjunct in list
     * Add conjunct to list until conjunct not followed by ampersand */
    do {
        conjList.push_back(parseConj());
    } while (match(CONJ));
    return std::make_unique<Rule>(std::move(conjList));
}

// disjunction ::= NON_TERM '->' rule rlist ';'
static Node parseDisj() {
    NodeList ruleList;

    /* Pipe follows every rule except last rule in list
     * Add rule to list until rule not followed by pipe */
    do {
        ruleList.push_back(parseRule());
    } while (match(DISJ));

    // Disjunction must be terminated with semicolon
    if (!match(SC))
        parseError("';'");
    return std::make_unique<Disj>(std::move(ruleList));
}

// grammar ::= disjunction dlist
// dlist ::= disjunction dlist
//        |  epsilon
// disjunction ::= NON_TERM '->' rule rlist ';'
static std::map<std::string, Node> parseGrammar() {
    getNextToken();                       // get first token
    std::map<std::string, Node> disjList; // map non-terminals to disjunctions

    // Add disjunction until EOF reached
    do {
        std::cout << "Parsing disjunction\n";

        // If current token is a non-terminal, move on to next token; if not, error
        std::string nt = CurTok.lexeme;
        if (CurTok.type == NON_TERM)
            getNextToken();
        else
            parseError("non-terminal");

        // Syntax error if non-terminal not followed by '->'
        if (!match(DERIVE))
            parseError("'->'");

        Node nextDisj = parseDisj();
        disjList[nt] = std::move(nextDisj);
    } while (!match(EOF_TOK));
    return disjList;
}

//---------------------//
// Grammar AST Printer //
//---------------------//

// Make indentation of given width
std::string makeIndent(int depth) {
    std::string indent = "";
    while (depth > 0) {
        indent += "▏  ";
        depth--;
    }
    return indent;
}

// Convert NodeList to string
std::string nlString(const NodeList& list, int depth) {
    std::string result = "";
    for (const Node& n : list) {
        if (n != nullptr)
            result += n->toString(depth); // convert each item and add to result string
    }
    return result;
}

// Print symbol
std::string printSymb(const SYMBOL& symbol, int depth) {
    std::string term;
    if (symbol.terminal)
        term = "";
    else
        term = "NON-";

    return makeIndent(depth) + term + "TERMINAL: " + symbol.str + "\n";
}

// Print conjunct
std::string Conjunct::toString(int depth) const {
    std::string posOrNeg;
    if (Pos)
        posOrNeg = "+VE";
    else
        posOrNeg = "-VE";

    std::string result = makeIndent(depth) + posOrNeg + " CONJUNCT:\n";
    for (const SYMBOL& s : Symbols) {
        result += printSymb(s, depth + 1);
    }
    return result;
}

// Print rule
std::string Rule::toString(int depth) const {
    return makeIndent(depth) + "RULE:\n" + nlString(ConjList, depth + 1);
}

// Print disjunction
std::string Disj::toString(int depth) const {
    return makeIndent(depth) + nlString(RuleList, depth + 1);
}

//--------------------//
// Compute FIRST sets //
//--------------------//

// Get set of non-terminals used in conjunct
StrSet Conjunct::references() const {
    if (Pos)
        return NtsReferenced;
    else
        return StrSet();
}

// Get set of non-terminals used in rule (union of conjuncts' sets of non-terminals)
StrSet Rule::references() const {
    StrSet ntsReferenced;
    for (const Node& conj : ConjList) {
        auto conjReferences = conj->references();
        ntsReferenced.insert(conjReferences.cbegin(), conjReferences.cend());
    }
    return ntsReferenced;
}

// Get set of non-terminals used in disjunction (union of rules' sets of non-terminals)
StrSet Disj::references() const {
    StrSet ntsReferenced;
    for (const Node& rule : RuleList) {
        auto ruleReferences = rule->references();
        ntsReferenced.insert(ruleReferences.cbegin(), ruleReferences.cend());
    }
    return ntsReferenced;
}

/* "Adjacency list" of non-terminal references
 * Each non-terminal mapped to set of non-terminals used in rules derived from it */
std::map<std::string, StrSet> referencedNts;

// Depth-first search on adjacency list
StrSet visited;
StrVec dfs(std::string nt, StrVec ntOrder) {
    visited.insert(nt); // mark non-terminal as visited

    // Depth-first search on all non-terminals referenced by nt that are unvisited
    for (const std::string str : referencedNts[nt]) {
        if (visited.count(str) == 0)
            ntOrder = dfs(str, ntOrder);
    }
    
    // Add nt to ordering
    ntOrder.push_back(nt);
    return ntOrder;
}

// Topological sort for non-terminals
StrVec topologicalSort() {
    StrVec ntOrder;

    // Start depth-first search
    for (const auto& nt : referencedNts) {
        if (visited.count(nt.first) == 0)
            ntOrder = dfs(nt.first, ntOrder);
    }
    return ntOrder; // return topological ordering
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

    // Parse input file
    auto grammar = parseGrammar();
    std::cout << "Parsing finished\n";
    fclose(bbnfFile);

    // Print AST of grammar
    std::string grammarStr = "";
    for (const auto& disj : grammar) {
        grammarStr += "TERMINAL " + disj.first + "\n" + disj.second->toString(0);
    }
    std::cout << grammarStr;

    /* Build adjacency list: map each non-terminal to its set of non-terminal references
     * Print mappings */
    std::string referencesStr = "";
    for (const auto& disj : grammar) {
        std::string nt = disj.first;
        referencedNts[nt] = disj.second->references();
        referencesStr += nt + ":";

        // Print each symbol in this non-terminal's set
        for (const std::string str : referencedNts[nt]) {
            referencesStr += " " + str;
        }
        referencesStr += "\n";
    }
    std::cout << referencesStr;

    // Topological ordering of non-terminals
    StrVec ntOrder = topologicalSort();
    for (const std::string s : ntOrder)
        std::cout << s + "\n";

    return 0;
}
