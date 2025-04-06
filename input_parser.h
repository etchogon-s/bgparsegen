#pragma once
#ifndef INPUT_PARSER_H
#define INPUT_PARSER_H

std::map<std::string, GNode> parseGrammar(); // top-level parsing function
extern FILE *inpFile;                        // input file

#endif
