#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <utility>
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
    int type, lineNo, columnNo;
    std::string lexeme; // actual contents of token
};

FILE *bbnfFile;              // input grammar file
int lineNo = 1;
int columnNo = 1;

// Create token
static TOKEN makeToken(std::string lexVal, int tokType) {
    TOKEN tok;
    tok.lexeme = lexVal;
    tok.type = tokType;
    tok.lineNo = lineNo;
    tok.columnNo = columnNo - lexVal.length() - 1;
    return tok;
}

// Build token by reading characters from file
static TOKEN getToken() {
    char currentChar, nextChar;
    std::string currentStr = ""; // holds current string being tokenised

    // Skip whitespace
    while (isspace(currentChar = fgetc(bbnfFile))) {
        columnNo++;
        if ((currentChar == '\n') || (currentChar == '\r')) {
            lineNo++;
            columnNo = 1; // new line starts after newline character
        }
    }

    // Build string literal token
    if (currentChar == '"') {
        columnNo++; // discard opening "

        // Add characters to string until closing " reached
        while ((currentChar = fgetc(bbnfFile)) != '"') {
            if (currentChar == '\\') { // \" escape sequence for " in string
                if ((nextChar = fgetc(bbnfFile)) == '"') {
                    columnNo++;
                    currentChar = nextChar; // skip \ in currentStr
                } else {
                    fseek(bbnfFile, -1, SEEK_CUR); // don't lose next character, move back 1
                }
            }
            currentStr += currentChar;
            columnNo++;
        }

        columnNo++; // discard closing "
        if (currentStr == "")
            return makeToken(currentStr, EPSILON); // epsilon = empty string
        return makeToken(currentStr, STR_LIT);
    }

    // Add characters to string until non-underscore/alphanumeric character reached
    while (isalnum(currentChar) || (currentChar == '_')) {
        currentStr += currentChar;
        columnNo++;
        currentChar = fgetc(bbnfFile);
    }

    // If characters have been added to string, return non-terminal or epsilon token
    if (currentStr != "") {
        fseek(bbnfFile, -1, SEEK_CUR); // don't lose current character, move back 1
        if (currentStr == "epsilon")
            return makeToken("", EPSILON);
        return makeToken(currentStr, NON_TERM);
    }

    // After -, check for > to build -> derivation symbol token
    if (currentChar == '-') {
        if ((nextChar = fgetc(bbnfFile)) == '>') {
            columnNo += 2;
            return makeToken("->", DERIVE);
        } else {
            fseek(bbnfFile, -1, SEEK_CUR);  // don't lose next character, move back 1
            columnNo++;
            return makeToken("-", INVALID); // - without > is invalid
        }
    }

    // Build single character tokens
    columnNo++;
    switch (currentChar) {
        case '|':
            return makeToken("|", DISJ);
        case '&':
            return makeToken("&", CONJ);
        case '~':
            return makeToken("~", NEG);
        case ';':
            return makeToken(";", SC);
        case EOF:
            return makeToken("EOF", EOF_TOK); // end of file reached, do not read more characters
    }

    // If character not recognised, create invalid token
    std::string s(1, currentChar);
    return makeToken(s, INVALID);
}

//-------------------//
// Grammar AST Nodes //
//-------------------//

// Non-terminal or terminal in conjunct
struct SYMBOL {
    int type;        // same as symbol's token type (terminal, non-terminal, epsilon)
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
        virtual StrSet firstSet() {return StrSet();};
        virtual void followAdd(std::string nt) const {};
        virtual void updateTable(std::string nt) {};
        virtual bool isNullable() const {return true;};
        virtual bool isPositive() const {return true;};
        virtual SymbVec getSymbols() const {return SymbVec();};
};

using Node = std::shared_ptr<ASTNode>;
using NodeList = std::vector<Node>;

// Conjunct (list of symbols)
class Conjunct: public ASTNode {
    bool Pos;             // true if positive conjunct, false if negative
    SymbVec Symbols;      // symbols making up conjunct
    StrSet NtsReferenced; // set of non-terminals used in conjunct
    StrSet Firsts;        // FIRST set of conjunct
    bool Nullable = true; // whether conjunct is nullable, i.e. all symbols are nullable

    public:
        Conjunct(bool pos, SymbVec symbols, StrSet ntsReferenced): Pos(pos), Symbols(std::move(symbols)), NtsReferenced(ntsReferenced) {}
        virtual std::string toString(int depth) const override;
        virtual StrSet references() const override;
        virtual StrSet firstSet() override;
        virtual void followAdd(std::string nt) const override;
        virtual bool isNullable() const override {return Nullable;};
        virtual bool isPositive() const override {return Pos;};
        virtual SymbVec getSymbols() const override {return Symbols;};
};

// Rule (list of conjuncts)
class Rule: public ASTNode {
    NodeList ConjList;
    StrSet Firsts;        // FIRST set of rule
    bool Nullable = true; // whether rule is nullable, i.e. all conjuncts are nullable

    public:
        Rule(NodeList conjList): ConjList(std::move(conjList)) {}
        virtual std::string toString(int depth) const override;
        virtual StrSet references() const override;
        virtual StrSet firstSet() override;
        virtual void followAdd(std::string nt) const override;
        virtual void updateTable(std::string nt) override;
};

// Disjunction (list of rules derived by a non-terminal)
class Disj: public ASTNode {
    NodeList RuleList;

    public:
        Disj(NodeList ruleList): RuleList(std::move(ruleList)) {}
        virtual std::string toString(int depth) const override;
        virtual StrSet references() const override;
        virtual StrSet firstSet() override;
        virtual void followAdd(std::string nt) const override;
        virtual void updateTable(std::string nt) override;
};

//--------------------------//
// Recursive Descent Parser //
//--------------------------//

static TOKEN CurTok; // current token that parser is looking at

// Check if current token is of given type
bool match(TOKEN_TYPE tokType) {
    if (CurTok.type == tokType) {
        CurTok = getToken(); // if matched, move on to next token
        return true;
    }
    return false; // do not move on, current token will likely be checked again
}

/* Display parsing error
 * Include token that triggered error, line and column numbers, and expected sequence */
void parseError(std::string expected) {
    std::cout << "Parse error [ln " + std::to_string(CurTok.lineNo) + ", col " + std::to_string(CurTok.columnNo) + "]: unexpected token '" + CurTok.lexeme + "' (expecting " + expected + ")\n";
    exit(1); // quit on finding error
}

StrSet alphabet = {""}; // set of terminal symbols; include epsilon (empty string)

// symbol ::= NON_TERM | '"' STR_LIT '"' | 'epsilon'
static SYMBOL parseSymbol() {
    int symbType = CurTok.type;
    std::string symbStr = CurTok.lexeme;
    if (!match(NON_TERM) && !match(STR_LIT) && !match(EPSILON))
        parseError("non-terminal or literal");

    // Add symbol to alphabet if terminal
    if (symbType == STR_LIT)
        alphabet.insert(symbStr);

    // Create & return new symbol
    SYMBOL symb;
    symb.type = symbType;
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
        if (nextSymb.type == NON_TERM)
            ntsReferenced.insert(nextSymb.str);
    } while ((CurTok.type != CONJ) && (CurTok.type != DISJ) && (CurTok.type != SC));

    /* If list of symbols is longer than 1 and contains epsilons, these are redundant
     * Remove all redundant epsilons from list */
    size_t epsilonSize = 1;
    if (symbols.size() > epsilonSize)
        std::erase_if(symbols, [](SYMBOL symb) {return symb.type == EPSILON;});
    return std::make_shared<Conjunct>(pos, std::move(symbols), ntsReferenced);
}

// rlist ::= '|' rule rlist
//        |  epsilon
// rule ::= conjunct clist
static Node parseRule() {
    NodeList conjList;

    /* Ampersand follows every conjunct except last conjunct in list
     * Add conjunct to list until conjunct not followed by ampersand */
    do {
        conjList.push_back(parseConj());
    } while (match(CONJ));
    return std::make_shared<Rule>(std::move(conjList));
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
    return std::make_shared<Disj>(std::move(ruleList));
}

// grammar ::= disjunction dlist
// dlist ::= disjunction dlist
//        |  epsilon
// disjunction ::= NON_TERM '->' rule rlist ';'
static std::map<std::string, Node> parseGrammar() {
    std::map<std::string, Node> disjList; // map non-terminals to disjunctions
    CurTok = getToken();                  // get first token

    // Add disjunction until EOF reached
    do {
        std::string nt = CurTok.lexeme; // get key (non-terminal)
        if (!match(NON_TERM))
            parseError("non-terminal");
        if (!match(DERIVE))
            parseError("'->'");

        // Get value (set of rules) and insert key & value into map
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
        indent += "    ";
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
    std::string result = makeIndent(depth);
    if (symbol.type == NON_TERM)
        result += "NON-";
    result += "TERMINAL: ";
    if (symbol.str == "")
        result += "epsilon"; // represents empty string
    else
        result += symbol.str;
    return result + "\n";
}

// Print conjunct (show whether positive or negative, and print sequence of symbols)
std::string Conjunct::toString(int depth) const {
    std::string result = makeIndent(depth);
    if (Pos)
        result += "+VE";
    else
        result += "-VE";
    result += " CONJUNCT:\n";
    for (const SYMBOL& symb : Symbols)
        result += printSymb(symb, depth + 1);
    return result;
}

// Print rule (series of conjuncts)
std::string Rule::toString(int depth) const {
    return makeIndent(depth) + "RULE:\n" + nlString(ConjList, depth + 1);
}

// Print disjunction (series of rules)
std::string Disj::toString(int depth) const {
    return makeIndent(depth) + nlString(RuleList, depth + 1);
}

//----------------------------------------------//
// Sort Non-Terminals for FIRST Set Computation //
//----------------------------------------------//

// Get set of non-terminals used in +ve conjunct
StrSet Conjunct::references() const {
    if (Pos)
        return NtsReferenced;
    else
        return StrSet();
}

// Get set of non-terminals used in rule (union of +ve conjuncts' sets of non-terminals)
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
    for (const std::string& s : referencedNts[nt]) {
        if (visited.count(s) == 0)
            ntOrder = dfs(s, ntOrder);
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

//--------------------//
// Compute FIRST Sets //
//--------------------//

std::map<std::string, StrSet> firstSets; // maps each non-terminal to its FIRST set

// Compute FIRST set of conjunct
StrSet Conjunct::firstSet() {
    if (!Pos) {
        Firsts = alphabet; // if conjunct is negative, FIRST set is FIRST(alphabet*)
        return Firsts;
    }

    // Add to FIRST set until a non-nullable symbol is reached in the conjunct
    for (const SYMBOL& symb : Symbols) {
        /* If first symbol in the conjunct is epsilon, this is the conjunct's only symbol
         * The conjunct's FIRST set contains epsilon only */
        if (symb.type == EPSILON) {
            Firsts.insert(""); // epsilon = empty string
            return Firsts;

        // Terminal is non-nullable, so FIRST set is complete after adding it
        } else if (symb.type == STR_LIT) {
            Firsts.insert(symb.str);
            Nullable = false;
            return Firsts;

        // If s is a non-terminal, add elements of its FIRST set to conjunct's FIRST set
        } else {
            StrSet symbFirsts = firstSets[symb.str];
            Firsts.insert(symbFirsts.cbegin(), symbFirsts.cend());

            // s is non-nullable if its FIRST set does not contain empty string
            if (!symbFirsts.contains("")) {
                Nullable = false;
                return Firsts; // conjunct FIRST set is complete when non-nullable reached
            }
        }
    }
    return Firsts; // all symbols in the conjunct are nullable
}

// Compute FIRST set of rule (intersection of conjuncts' FIRST sets)
StrSet Rule::firstSet() {
    Firsts = alphabet; // start with entire alphabet

    // For each conjunct, remove items that are not in the conjunct's FIRST set
    for (const Node& conj : ConjList) {
        StrSet conjFirsts = conj->firstSet();
        for (auto it = Firsts.begin(); it != Firsts.end();) {
            if (!conjFirsts.contains(*it))
                it = Firsts.erase(it);
            else
                it++;
        }
    }
    return Firsts;
}

// Compute FIRST set of disjunction (union of rules' FIRST sets)
StrSet Disj::firstSet() {
    StrSet firsts;
    for (const Node& rule : RuleList) {
        StrSet ruleFirsts = rule->firstSet();
        firsts.insert(ruleFirsts.cbegin(), ruleFirsts.cend());
    }
    return firsts;
}

//---------------------//
// Compute FOLLOW Sets //
//---------------------//

std::map<std::string, StrSet> followSets; // maps each non-terminal to its FOLLOW set

// Build FOLLOW sets of non-terminals used in conjunct
void Conjunct::followAdd(std::string nt) const {
    size_t conjSize = Symbols.size();
    size_t nextIndex;
    bool nonNullableFound;

    // Iterate over symbols in conjunct to find non-terminals
    for (size_t i = 0; i < conjSize; i++) {
        const SYMBOL& current = Symbols[i];

        // If symbol is non-terminal, look at subsequent symbols to add to its FOLLOW set
        if (current.type == NON_TERM) {
            nextIndex = i + 1;
            nonNullableFound = false;

            // If current symbol's FOLLOW set doesn't exist yet, create it
            if (followSets.count(current.str) == 0)
                followSets[current.str] = StrSet();

            // Add to FOLLOW set until non-nullable symbol or end of conjunct reached
            while (!nonNullableFound && (nextIndex < conjSize)) {
                const SYMBOL& next = Symbols[nextIndex];
                if (next.type == STR_LIT) {
                    followSets[current.str].insert(next.str); // put terminal in FOLLOW set
                    nonNullableFound = true;                  // terminal is non-nullable

                // If non-terminal, add elements of its FIRST set to current's FOLLOW set
                } else if (next.type == NON_TERM) {
                    StrSet nextFirsts = firstSets[next.str];
                    followSets[current.str].insert(nextFirsts.cbegin(), nextFirsts.cend());

                    // Non-nullable if FIRST set does not contain empty string
                    if (!nextFirsts.contains(""))
                        nonNullableFound = true;
                }
                nextIndex++; // go to next symbol
            }

            /* If all the symbols after the current symbol are nullable, add elements of the
             * FOLLOW set of the deriving non-terminal (if it is different from current) to
             * current's FOLLOW set */
            if (!nonNullableFound && (nt != current.str)) {
                StrSet& ntFollowing = followSets[nt];
                followSets[current.str].insert(ntFollowing.cbegin(), ntFollowing.cend());
            }
        }
    }
    return;
}

// Build FOLLOW sets of non-terminals used in rule (for each conjunct, add to sets)
void Rule::followAdd(std::string nt) const {
    for (const Node& conj : ConjList)
        conj->followAdd(nt);
    return;
}

// Build FOLLOW sets of non-terminals used in disjunction (for each rule, add to sets)
void Disj::followAdd(std::string nt) const {
    for (const Node& rule : RuleList)
        rule->followAdd(nt);
    return;
}

//-----------------------//
// Compute Parsing Table //
//-----------------------//

// Parsing table, maps pair of non-terminal and string to rule (list of conjuncts)
std::map<std::pair<std::string, std::string>, NodeList> parseTable;

// Add rule to parsing table entry for non-terminal nt and string s if suitable
void Rule::updateTable(std::string nt) {
    // If any conjunct is not nullable, rule is not nullable
    size_t i = 0;
    while (Nullable && (i < ConjList.size())) {
        Nullable = ConjList[i]->isNullable() && Nullable;
        i++;
    }

    /* If string s is in rule's FIRST set, OR rule is nullable and s is in nt's FOLLOW set,
     * add rule to entry (nt, s) of parsing table */
    for (const std::string& s : alphabet) {
        if (Firsts.contains(s) || (Nullable && followSets[nt].contains(s))) {
            std::pair<std::string, std::string> tableEntry = make_pair(nt, s);
            NodeList entryContent;
            for (const Node& conj : ConjList)
                entryContent.push_back(conj);
            parseTable[tableEntry] = entryContent;
        }
    }
    return;
}

// Build parsing table by adding each rule in disjunction to any suitable entries
void Disj::updateTable(std::string nt) {
    for (const Node& rule : RuleList)
        rule->updateTable(nt);
    return;
}

//-----------------//
// Code Generation //
//-----------------//

std::string parserGlobals = R"(#include <iostream>
#include <string>
#include <vector>

FILE *inputFile;
std::vector<std::string> sentence;
size_t pos, start, end;)";

std::string handleError = R"(

void parseFail() {
    errors.push_back("Parse error [ln 1, col " + std::to_string(pos + 1) + "]: unexpected token " + current + ", expecting " + expected + "\n");
})";

// Print elements of set of strings
std::string strSetString(StrSet strs) {
    std::string result = "";
    for (const std::string& s : strs) {
        result += " ";
        if (s == "")
            result += "epsilon"; // represents empty string
        else
            result += s;
    }
    return result;
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

    // Parse input file
    std::map<std::string, Node> grammar = parseGrammar();
    fclose(bbnfFile);
    std::cout << "Alphabet:" + strSetString(alphabet) + "\n"; // print alphabet

    // Print AST of grammar
    std::cout << "\nGrammar AST\n";
    for (const auto& disj : grammar)
        std::cout << "TERMINAL " + disj.first + "\n" + disj.second->toString(0);

    /* Build adjacency list: map each non-terminal to its set of non-terminal references
     * Print mappings */
    std::cout << "\nReferenced Non-Terminals\n";
    for (const auto& disj : grammar) {
        std::string nt = disj.first;
        referencedNts[nt] = disj.second->references();
        std::cout << nt + ":" + strSetString(referencedNts[nt]) + "\n";
    }

    // Compute topological ordering of non-terminals and print non-terminals in this order
    StrVec ntOrder = topologicalSort();
    std::cout << "\nOrder of Computing FIRST Sets:";
    for (const std::string& s : ntOrder)
        std::cout << " " + s;
    std::cout << "\n";

    // Compute and print FIRST sets of non-terminals (in topological order)
    std::cout << "\nFIRST Sets\n";
    for (const std::string& s : ntOrder) {
        firstSets[s] = grammar[s]->firstSet();
        std::cout << s + ":" + strSetString(firstSets[s]) + "\n";
    }

    // Compute FOLLOW sets of non-terminals
    std::reverse(ntOrder.begin(), ntOrder.end()); // reverse order of non-terminals
    for (size_t i = 0; i < ntOrder.size(); i++) {
        const std::string& s = ntOrder[i];
        if (i == 0) { // first symbol in ordering is start symbol
            followSets[s] = StrSet();
            followSets[s].insert(""); // FOLLOW set of start symbol is just epsilon
        }
        grammar[s]->followAdd(s);
    }

    // Print FOLLOW sets
    std::cout << "\nFOLLOW Sets\n";
    for (const std::string& s : ntOrder)
        std::cout << s + ":" + strSetString(followSets[s]) + "\n";

    // Build parsing table
    for (const auto& disj : grammar)
        disj.second->updateTable(disj.first);

    // Print parsing table
    std::cout << "\nParsing Table\n";
    for (const auto& entry : parseTable) {
        std::cout << "NON-TERMINAL " + (entry.first).first + ", STRING ";
        if ((entry.first).second == "")
            std::cout << "epsilon";
        else
            std::cout << (entry.first).second;
        std::cout << "\n" + makeIndent(1) + "RULE:\n" + nlString(entry.second, 2);
    }

    std::ofstream ParserFile;
    ParserFile.open("parser.cpp");
    ParserFile << parserGlobals;

    std::map<std::string, int> terminalNos;
    int terminalNo = 0;
    for (const std::string& s : alphabet) {
        if (s != "") {
            terminalNos[s] = terminalNo;
            ParserFile << std::format(
R"(

bool terminal{}() {{
    if (sentence[pos] == "{}") {{
        pos++;
        return true;
    }} else {{
        return false;
    }}
}})",
            terminalNo, s);
            terminalNo++;
        }
    }

    std::reverse(ntOrder.begin(), ntOrder.end()); // reverse order of non-terminals
    std::map<std::string, int> nonTerminalNos;
    int nonTerminalNo = 0;
    for (const std::string& nt : ntOrder) {
        nonTerminalNos[nt] = nonTerminalNo;

        std::string ntCases = "";
        for (const std::string& s : alphabet) {
            std::pair<std::string, std::string> symbolPair = make_pair(nt, s);
            if (parseTable.count(symbolPair)) {

                std::string tableEntryConjuncts = "";
                size_t conjNo = 0;
                for (const Node& conj : parseTable[symbolPair]) {
                    std::string tableEntryConj;

                    std::string symbolSequence = "";
                    size_t symbNo = 0;
                    for (const SYMBOL& symb : conj->getSymbols()) {
                        if (symbNo > 0)
                            symbolSequence += " && ";
                        if (symb.type == STR_LIT)
                            symbolSequence += "terminal" + std::to_string(terminalNos[symb.str]) + "()";
                        else if (symb.type == NON_TERM)
                            symbolSequence += "nonTerminal" + std::to_string(nonTerminalNos[symb.str]) + "()";
                        symbNo++;
                    }
                    
                    if (conj->isPositive() && (symbolSequence != "")) {
                        tableEntryConj = std::format(
R"(        if (!({}))
            return false;
)",
                        symbolSequence);

                        if (parseTable[symbolPair].size() > 1) {
                            if (conjNo == 0)
                                tableEntryConj = std::format(
R"(        start = pos;
{}        end = pos;
)", 
                                tableEntryConj);

                            else
                                tableEntryConj = std::format(
R"(
        pos = start;{}
        if (pos != end)
            return false;
)",
                                tableEntryConj);
                        }
                    } else if (!conj->isPositive()) {
                        std::string isLastConj = "";
                        if (conjNo == parseTable[symbolPair].size() - 1)
                            isLastConj = "\n        pos = end;";

                        tableEntryConj = std::format(
R"(
        pos = start;
        bool success = ({});
        if (success && (pos == end))
            return false;{}
)", 
                        symbolSequence, isLastConj);
                    }
                    tableEntryConjuncts += tableEntryConj;
                    conjNo++;
                }

                ntCases += std::format(
R"(
    if (sentence[pos] == "{}") {{
{}        return true;
    }}
)", 
                s, tableEntryConjuncts);
            }
        }

        ParserFile << std::format(
R"(

bool nonTerminal{}() {{
{}
    return false;
}})", 
        nonTerminalNo, ntCases);
        nonTerminalNo++;
    }

    ParserFile << std::format(
R"(

int main(int argc, char **argv) {{
    if (argc == 2) {{
        inputFile = fopen(argv[1], "r");
        if (inputFile == NULL)
            std::cout << "Error opening file\n";
    }} else {{
        std::cout << "Usage: ./parser <input file>\n";
        return 1;
    }}
    
    pos = 0;
    char nextChar;
    while ((nextChar = fgetc(inputFile)) != EOF) {{
        if (!isspace(nextChar)) {{
            std::string s(1, nextChar);
            sentence.push_back(s);
        }}
    }}
    fclose(inputFile);

    if (nonTerminal{}() && (pos == sentence.size()))
        std::cout << "Parsing successful\n";
    else
        std::cout << "Parsing failed\n";
    return 0;
}})", 
    nonTerminalNo - 1);
    ParserFile.close();

    return 0;
}
