#include <format>
#include <fstream>
#include "grammar.h"
#include "rd_codegen.h"

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

std::map<std::string, int> terminalNos, nonTerminalNos;

std::string parseTerminal(int terminalNo, const std::string& s) {
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

std::string parseSymbSeq(const SymbVec& symbols) {
    std::string symbolSequence = "";
    int symbNo = 0;
    for (const SYMBOL& symb : symbols) {
        if (symbNo > 0)
            symbolSequence += " && ";

        if (symb.type == LITERAL)
            symbolSequence += "terminal" + std::to_string(terminalNos[symb.str]) + "()";
        else if (symb.type == NON_TERM)
            symbolSequence += "nonTerminal" + std::to_string(nonTerminalNos[symb.str]) + "()";
        symbNo++;
    }
    return symbolSequence;
}

std::string notOnlyConj(std::string conjCode, size_t conjNo) {
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

std::string parseConj(const GNode& conj, size_t conjNo, size_t ruleSize) {
    std::string conjCode = "";
    std::string symbolSequence = parseSymbSeq(conj->getSymbols());
    
    if (conj->isPositive() && (symbolSequence != "")) {
        conjCode = std::format(
R"(        if (!({}))
return false;
)",
        symbolSequence);

        if (ruleSize > 1)
            return notOnlyConj(conjCode, conjNo);
        return conjCode;
    }

    if (!conj->isPositive()) {
        std::string isLastConj = "";
        if (conjNo == ruleSize - 1)
            isLastConj = "\n        pos = end;";

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

std::string parseNonTerminal(int nonTerminalNo, const std::string& nt) {
    std::string ntCases = "";

    for (const std::string& s : alphabet) {
        std::pair<std::string, std::string> symbolPair = make_pair(nt, s);
        if (parseTable.count(symbolPair)) {
            std::string tableEntry = "";
            size_t ruleSize = parseTable[symbolPair].size();
            size_t conjNo = 0;
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

    return std::format(R"(

bool nonTerminal{}() {{
{}
return false;
}})", 
    nonTerminalNo, ntCases);
}

std::string mainFunction(int nonTerminalNo) {
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

void RDCodegen(StrVec ntOrder) {
    std::ofstream parserFile;
    parserFile.open("rd_parser.cpp");
    parserFile << parserGlobals;

    int terminalNo = 0;
    for (const std::string& s : alphabet) {
        if (s != "") {
            terminalNos[s] = terminalNo;
            parserFile << parseTerminal(terminalNo, s);
            terminalNo++;
        }
    }

    int nonTerminalNo = 0;
    for (const std::string& nt : ntOrder) {
        nonTerminalNos[nt] = nonTerminalNo;
        parserFile << parseNonTerminal(nonTerminalNo, nt);
        nonTerminalNo++;
    }

    parserFile << mainFunction(nonTerminalNo);
    parserFile.close();
}
