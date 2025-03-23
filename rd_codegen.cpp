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

FILE *inputFile;
std::vector<std::string> sentence;
size_t pos, start, end;)";

// Parser error handling function (not currently in use)
static std::string handleError = R"(

void parseFail() {
    errors.push_back("Parse error [ln 1, col " + std::to_string(pos + 1) + "]: unexpected token " + current + ", expecting " + expected + "\n");
})";

// Assign a number to each terminal and non-terminal
static std::map<std::string, int> terminalNos, nonTerminalNos;

/* Generate code for parsing a terminal
 * If character matched, success & move forward 1 character, otherwise failure */
static std::string parseTerminal(int terminalNo, const std::string& s) {
    return std::format(R"(

PNode terminal{}() {{
    if (sentence[pos] == "{}") {{
        pos++;
        return std::make_shared<Leaf>("{}");
    }} else {{
        return nullptr;
    }}
}})",
    terminalNo, s, s);
}

// Generate code for parsing a sequence of symbols
static std::string parseSymbSeq(const SymbVec& symbols, bool posConj, size_t conjNo) {
    std::string symbolSequence = "";

    int symbNo = 0;
    for (const SYMBOL& symb : symbols) {
        std::string symbFunction = "";
        if (symb.type == LITERAL)
            symbFunction += "terminal" + std::to_string(terminalNos[symb.str]) + "()";
        else if (symb.type == NON_TERM)
            symbFunction += "nonTerminal" + std::to_string(nonTerminalNos[symb.str]) + "()";

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

// Add to conjunct code if it is one of many positive conjuncts
static std::string manyPosConj(std::string conjCode, size_t conjNo) {
    if (conjNo == 0)
        return std::format(
R"(        start = pos;
{}        end = pos;
)", 
        conjCode); // For first conjunct, record positions where parsing starts and ends

    /* For subsequent conjuncts, return to the recorded start position before parsing
     * If parsing stops before or after the recorded end position, this is a failure,
     * since a different substring of the input has been parsed */
    return std::format(R"(
        pos = start;{}
        if (pos != end)
            return nullptr;
)",
    conjCode);
}

// Generate code for parsing a conjunct
static std::string parseConj(const GNode& conj, size_t conjNo, size_t ruleSize) {
    bool posConj = conj->isPositive();
    std::string conjCode = "";
    std::string symbolSequence = parseSymbSeq(conj->getSymbols(), posConj, conjNo);

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
        if (success && (pos == end))
            return nullptr;{}
)", 
        symbolSequence, isLastConj);
    }
    
    // Positive conjunct that contains at least 1 (non-)terminal
    if (posConj && (symbolSequence != "")) {
        conjCode = std::format(
R"(        PNodeList conj{};
{}
)",
        conjNo, symbolSequence); // create new subtree version for conjunct

        // Adjust code if rule contains multiple conjuncts
        if (ruleSize > 1)
            conjCode = manyPosConj(conjCode, conjNo);

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
    if (sentence[pos] == "{}") {{
{}        return std::make_shared<Internal>("{}", std::move(subTreeVersions));
    }}
)", 
            s, tableEntry, nt); // if return statement is reached, parsing is successful
        }
    }

    // Add cases to the non-terminal's numbered function
    return std::format(R"(

PNode nonTerminal{}() {{
    std::vector<PNodeList> subTreeVersions;
{}
    return nullptr;
}})", 
    nonTerminalNo, ntCases); // if function does not return after any case, parsing fails
}

/* Main parser function
 * Reads characters from input file into buffer
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
    
    char nextChar;
    while ((nextChar = fgetc(inputFile)) != EOF) {{
        if (!isspace(nextChar)) {{
            std::string s(1, nextChar);
            sentence.push_back(s);
        }}
    }}
    fclose(inputFile);

    pos = 0;
    PNode root = nonTerminal{}();

    if (root && (pos == sentence.size())) {{
        std::cout << "Parsing successful\n";
        std::cout << root->toString(0);
        return 0;
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
    int terminalNo = 0;
    for (const std::string& s : alphabet) {
        if (s != "") {
            terminalNos[s] = terminalNo; // assign number to terminal
            parserFile << parseTerminal(terminalNo, s);
            terminalNo++;
        }
    }

    // Write parser functions for non-terminals
    int nonTerminalNo = 0;
    for (const std::string& nt : ntOrder) {
        nonTerminalNos[nt] = nonTerminalNo; // assign number to non-terminal
        parserFile << parseNonTerminal(nonTerminalNo, nt);
        nonTerminalNo++;
    }

    parserFile << mainFunction(nonTerminalNo);
    parserFile.close();
}
