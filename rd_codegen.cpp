#include <format>
#include <fstream>
#include "grammar.h"
#include "rd_codegen.h"

//------------------------------------------//
// Recursive Descent Parser Code Generation //
//------------------------------------------//

/* Code that starts parser file
 * sentence is a buffer that holds input
 * pos, start and end keep track of parser position in input */
static std::string beginningCode = R"(#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

std::string makeIndent(int depth) {
    std::string indent = "";
    while (depth > 0) {
        indent += "|   ";
        depth--;
    }
    return indent;
}

class ParseNode {
    public:
        virtual ~ParseNode() {}
        virtual std::string toString(int depth) {return "";};
};

using PNode = std::shared_ptr<ParseNode>;
using PNodeList = std::vector<PNode>;

class Leaf: public ParseNode {
    std::string Symbol;

    public:
        Leaf(std::string s): Symbol(s) {}
        std::string toString(int depth) override {
            return makeIndent(depth) + "TERMINAL " + Symbol + "\n";
        }
};

class Internal: public ParseNode {
    std::string Symbol;
    std::vector<PNodeList> Children;

    public:
        Internal(std::string s, std::vector<PNodeList> c): Symbol(s), Children(std::move(c)) {}
        std::string toString(int depth) override {
            std::string result = makeIndent(depth) + "NON-TERMINAL " + Symbol + "\n";
            for (const auto& conjNodes : Children) {
                result += makeIndent(depth + 1) + "CONJUNCT\n";
                for (const PNode& n : conjNodes) {
                    if (n)
                        result += n->toString(depth + 2);
                }
            }
            return result;
        }
};

struct TOKEN {
    std::string str;
    int lineNo, columnNo;
};

TOKEN makeToken(std::string str, int lineNo, int columnNo) {
    TOKEN token;
    token.str = str;
    token.lineNo = lineNo;
    token.columnNo = columnNo - str.length();
    return token;
}

FILE *inputFile;
std::vector<TOKEN> sentence;
size_t pos, start, end;

std::string displayPos(int lineNo, int columnNo) {
    if (lineNo == 0)
        return " [end]";
    return " [ln " + std::to_string(lineNo) + ", col " + std::to_string(columnNo) + "]";
}

void tokenFail(bool wanted, std::string expected) {
    if (!wanted)
        return;

    TOKEN current = sentence[pos];
    std::string failPos = displayPos(current.lineNo, current.columnNo);
    std::string failStr = (current.str == "") ? "EOF" : current.str;
    std::cout << "Parser error" + failPos + ": unexpected token " + failStr + ", expecting " + expected + "\n";
}

void conjFail(bool wanted, size_t start, size_t end, bool posConj, std::string conjStr) {
    if (!wanted)
        return;

    std::string currentPos = displayPos(sentence[pos - 1].lineNo, sentence[pos - 1].columnNo);
    std::string startPos = displayPos(sentence[start].lineNo, sentence[start].columnNo);
    std::string report = "Parser error" + currentPos + ": parsing of conjunct" + conjStr + " starting at" + startPos;

    if (posConj)
        std::cout << report + " should end at" + displayPos(sentence[end - 1].lineNo, sentence[end - 1].columnNo) + "\n";
    else
        std::cout << report + " is unwanted\n";
}

PNode terminal(bool wanted, std::string tokenStr) {
    if (sentence[pos].str == tokenStr) {
        pos++;
        return std::make_shared<Leaf>(tokenStr);
    } else {
        tokenFail(wanted, tokenStr);
        return nullptr;
    }
})";

// Assign a number to each non-terminal
static std::map<std::string, int> nonTerminalNos;

// Generate code for parsing a sequence of symbols
static std::string parseSymbSeq(const SymbVec& symbols, bool posConj, size_t conjNo) {
    std::string symbolSequence = "";

    int symbNo = 0;
    for (const SYMBOL& symb : symbols) {
        std::string symbFunction = "";
        if (symb.type != EPSILON) {
            std::string wantedStr = "wanted";
            if (!posConj)
                wantedStr = "!wanted";

            if (symb.type == LITERAL)
                symbFunction += "terminal(" + wantedStr + ", \"" + symb.str + "\"" + ")";
            else
                symbFunction += "nonTerminal" + std::to_string(nonTerminalNos[symb.str]) +"(" + wantedStr + ")";
        }

        if (symbFunction != "") {

            // If conjunct is negative, check if any symbol function has failed
            if (!posConj) {
                if (symbNo > 0)
                    symbolSequence += " && ";
                symbolSequence += symbFunction;

            /* If conjunct is positive, add a node for each symbol to the conjunct subtree
             * If one symbol function fails, whole conjunct fails */
            } else {
                std::string symbNode = std::format("conj{}node{}", conjNo, symbNo);
                symbolSequence += std::format(
R"(        PNode {} = {};
        if (!{})
            return nullptr;
        conj{}.push_back({});
)",
                symbNode, symbFunction, symbNode, conjNo, symbNode);
            }
        }
        symbNo++;
    }
    return symbolSequence;
}

// Generate code for parsing a conjunct
static std::string parseConj(const GNode& conj, size_t conjNo, size_t ruleSize) {
    bool posConj = conj->isPositive();
    std::string conjCode = "";
    const SymbVec& conjSymbols = conj->getSymbols();
    std::string symbolSequence = parseSymbSeq(conjSymbols, posConj, conjNo);

    std::string conjStr = "";
    for (const SYMBOL& symb : conjSymbols)
        conjStr += " " + symb.str;

    // Negative conjunct
    if (!posConj) {
        std::string isLastConj = "";

        // After last negative conjunct, move to end of substring
        if (conjNo == ruleSize - 1)
            isLastConj = "\n        pos = end;";

        // If negative conjunct is successfully parsed, this is a failure
        return std::format(R"(
        pos = start;
        bool success = ({});
        if (success && (pos == end)) {{
            conjFail(wanted, start, end, false, "{}");
            return nullptr;
        }}{}
)", 
        symbolSequence, conjStr, isLastConj);
    }
    
    // Positive conjunct that contains at least 1 (non-)terminal
    if (posConj && (symbolSequence != "")) {
        conjCode = std::format(
R"(        PNodeList conj{};
{}
)",
        conjNo, symbolSequence); // create new subtree version for conjunct

        // Adjust code if rule contains multiple conjuncts
        if (ruleSize > 1) {
            if (conjNo == 0)
                conjCode = std::format(
R"(        start = pos;
{}        end = pos;
)", 
                conjCode); // Record positions where first conjunct starts and ends

            /* For subsequent conjuncts, return to recorded start position before parsing
             * If parsing stops before or after recorded end position, this is a failure,
             * since a different substring of the input has been parsed */
            else
                conjCode = std::format(R"(
        pos = start;{}
        if (pos != end) {{
            conjFail(wanted, start, end, true, "{}");
            return nullptr;
        }}
)",
                conjCode, conjStr);
        }

        return std::format(
R"({}        subTreeVersions.push_back(conj{});

)",
        conjCode, conjNo); // add to subtree versions for this non-terminal
    }

    return conjCode;
}

// Generate code for parsing a non-terminal
static std::string parseNonTerminal(int nonTerminalNo, const std::string& nt) {
    std::string ntCases = "";
    std::string expected = "";

    // For each terminal that is paired with the non-terminal in the parse table, add a case
    for (const std::string& s : alphabet) {
        std::pair<std::string, std::string> symbolPair = make_pair(nt, s);
        if (parseTable.count(symbolPair)) {
            std::string tableEntry = "";
            size_t ruleSize = parseTable[symbolPair].size(); // number of conjuncts in rule
            size_t conjNo = 0;

            // Generate code for each conjunct in the rule in the table entry
            for (const GNode& conj : parseTable[symbolPair]) {
                tableEntry += parseConj(conj, conjNo, ruleSize);
                conjNo++;
            }

            // Add code for all conjuncts to the case
            ntCases += std::format(
R"(
    if (sentence[pos].str == "{}") {{
{}        return std::make_shared<Internal>("{}", std::move(subTreeVersions));
    }}
)", 
            s, tableEntry, nt); // if return statement is reached, parsing is successful

            std::string displayS = (s == "") ? "EOF" : s;
            expected = (expected == "") ? expected += displayS : expected += ", " + displayS;
        }
    }

    // Add cases to the non-terminal's numbered function
    return std::format(R"(

PNode nonTerminal{}(bool wanted) {{
    std::vector<PNodeList> subTreeVersions;
{}
    tokenFail(wanted, "{}");
    return nullptr;
}})", 
    nonTerminalNo, ntCases, expected); // if function does not return after any case, parsing fails
}

/* Main parser function
 * Lexer: reads characters from input file and converts them to tokens
 * Calls the parsing function for the start symbol (the last numbered non-terminal)
 * Parser must stop at the end of the input for parsing to succeed
 * If parsing succeeds, print parse tree */
static std::string mainFunction(int nonTerminalNo) {
    return std::format(R"(

int main(int argc, char **argv) {{
    if (argc == 2) {{
        inputFile = fopen(argv[1], "r");
        if (inputFile == NULL)
            std::cout << "Error opening file\n";
    }} else {{
        std::cout << "Usage: ./parser <input file>\n";
        return 1;
    }}
    
    char currentChar;
    std::string currentStr = "";
    int lineNo = 1;
    int columnNo = 1;
    while ((currentChar = fgetc(inputFile)) != EOF) {{
        columnNo++;
        if ((currentChar == '\n') || (currentChar == '\r')) {{
            lineNo++;
            columnNo = 1;
        }}

        if (!isspace(currentChar))
            currentStr += currentChar;

        if (terminals.count(currentStr) > 0) {{
            sentence.push_back(makeToken(currentStr, lineNo, columnNo));
            currentStr = "";
        }} else {{
            if (currentStr.length() >= 1) {{
                std::cout << "Lexer error [ln " + std::to_string(lineNo) + ", col " + std::to_string(columnNo - currentStr.length()) + "]: unexpected sequence '" + currentStr + "'\n";
                return 1;
            }}
        }}
    }}
    fclose(inputFile);

    pos = 0;
    PNode root = nonTerminal{}(true);
    if (root) {{
        if (pos == sentence.size()) {{
            std::cout << "Parsing successful\n";
            std::cout << root->toString(0);
            return 0;
        }}

        std::cout << "Parser error" + displayPos(sentence[pos].lineNo, sentence[pos].columnNo) + ": parsing terminated before end of input\n";
    }}

    std::cout << "Parsing failed\n";
    return 1;
}})", 
    nonTerminalNo - 1);
}

// Write code to file
void RDCodegen(StrVec ntOrder) {
    std::ofstream parserFile;
    parserFile.open("parser.cpp");
    parserFile << beginningCode;

    // Write parser functions for terminals
    std::string terminalSet = "\n\nstd::set<std::string> terminals = {";
    int terminalNo = 0;
    for (const std::string& s : alphabet) {
        if (s != "") {
            if (terminalNo > 0)
                terminalSet += ", ";
            terminalSet += "\"" + s + "\"";
            terminalNo++;
        }
    }
    terminalSet += "};";

    // Write parser functions for non-terminals
    int nonTerminalNo = 0;
    for (const std::string& nt : ntOrder) {
        nonTerminalNos[nt] = nonTerminalNo; // assign number to non-terminal
        parserFile << parseNonTerminal(nonTerminalNo, nt);
        nonTerminalNo++;
    }

    parserFile << terminalSet;
    parserFile << mainFunction(nonTerminalNo);
    parserFile.close();
}
