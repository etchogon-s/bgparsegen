bgparsegen: main.cpp input_parser.cpp rd_codegen.cpp
	g++ -std=c++20 -g -o bgparsegen main.cpp input_parser.cpp rd_codegen.cpp
