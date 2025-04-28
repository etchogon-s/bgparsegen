# bgparsegen
Parser generator for Boolean grammars.

Usage:

    $ make
    $ ./bgparsegen <grammar file> <k>

To run the generated parser:

    $ g++ -o <executable name> parser.cpp
    $ ./<executable name> <input file>
