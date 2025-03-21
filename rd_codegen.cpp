#include <format>
#include <fstream>
#include "grammar.h"
#include "rd_codegen.h"

// Code that starts parser file
static std::string beginningCode = R"(#include <iostream>
#include <string>
#include <vector>

FILE *inputFile;
std::vector<std::string> sentence;
size_t pos, start, end;)";

// Parser error handling function
static std::string handleError = R"(

void parseFail() {
    errors.push_back("Parse error [ln 1, col " + std::to_string(pos + 1) + "]: unexpected token " + current + ", expecting " + expected + "\n");
})";

// Assign a number to each terminal and non-terminal
static std::map<std::string, int> terminalNos, nonTerminalNos;

// Generate code for parsing a terminal: if string matched, advance position in input
static std::string parseTerminal(int terminalNo, const std::string& s) {
    return std::format(R"(

bool terminal{}() {{
    if (sentence[pos] == "{}") {{
        pos++;
        return true;
    }} else {{
        return false;
    }}
}})",
    terminalNo, s);
}

// Generate code for parsing a sequence of symbols
static std::string parseSymbSeq(const SymbVec& symbols) {
    std::string symbolSequence = "";
    int symbNo = 0;
    for (const SYMBOL& symb : symbols) {
        if (symbNo > 0)
            symbolSequence += " && "; // functions that are not the first one are preceded by &&

        // Add numbered terminal or non-terminal function
        if (symb.type == LITERAL)
            symbolSequence += "terminal" + std::to_string(terminalNos[symb.str]) + "()";
        else if (symb.type == NON_TERM)
            symbolSequence += "nonTerminal" + std::to_string(nonTerminalNos[symb.str]) + "()";
        symbNo++;
    }
    return symbolSequence;
}

// Add to conjunct code if it is not the only conjunct in a rule
static std::string notOnlyConj(std::string conjCode, size_t conjNo) {
    if (conjNo == 0)
        return std::format(
R"(        start = pos;
{}        end = pos;
)", 
        conjCode);

    return std::format(R"(
pos = start;{}
if (pos != end)
return false;
)",
    conjCode);
}

// Generate code for parsing a conjunct
static std::string parseConj(const GNode& conj, size_t conjNo, size_t ruleSize) {
    std::string conjCode = "";
    std::string symbolSequence = parseSymbSeq(conj->getSymbols());
    
    // Positive conjunct that contains at least 1 (non-)terminal
    if (conj->isPositive() && (symbolSequence != "")) {
        conjCode = std::format(
R"(        if (!({}))
return false;
)",
        symbolSequence);

        // Adjust code if rule contains multiple conjuncts
        if (ruleSize > 1)
            return notOnlyConj(conjCode, conjNo);
        return conjCode;
    }

    // Negative conjunct
    if (!conj->isPositive()) {
        std::string isLastConj = "";
        if (conjNo == ruleSize - 1)
            isLastConj = "\n        pos = end;"; // follows code for last negative conjunct

        return std::format(R"(
pos = start;
bool success = ({});
if (success && (pos == end))
return false;{}
)", 
        symbolSequence, isLastConj);
    }
    return conjCode;
}

// Generate code for parsing a non-terminal
static std::string parseNonTerminal(int nonTerminalNo, const std::string& nt) {
    std::string ntCases = "";

    // For each pair of a non-terminal and terminal in the parse table, add a case
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

            ntCases += std::format(
R"(
if (sentence[pos] == "{}") {{
{}        return true;
}}
)", 
            s, tableEntry);
        }
    }

    // Add cases to the non-terminal's numbered function
    return std::format(R"(

bool nonTerminal{}() {{
{}
return false;
}})", 
    nonTerminalNo, ntCases);
}

// Main parser function, calls the parser for the start symbol (the last numbered non-terminal)
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
}

// Write recursive descent parser code to file
void RDCodegen(StrVec ntOrder) {
    std::ofstream parserFile;
    parserFile.open("rd_parser.cpp");
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
