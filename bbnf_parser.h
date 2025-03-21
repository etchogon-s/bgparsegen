#ifndef BBNF_PARSER_H
#define BBNF_PARSER_H

#include <map>

std::map<std::string, GNode> parseGrammar(); // top-level parsing function
extern FILE *bbnfFile;                       // input file

#endif
