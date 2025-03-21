bgparsegen: main.cpp bbnf_parser.cpp rd_codegen.cpp
	g++ -std=c++20 -g -o bgparsegen main.cpp bbnf_parser.cpp rd_codegen.cpp
