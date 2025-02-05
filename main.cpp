#include <cstdio>
#include <iostream>

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
    auto grammarAST = parseGrammar();
    std::cout << "Parsing finished\n";

    fclose(bbnfFile);
    return 0;
}
