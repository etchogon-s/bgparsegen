#include <iostream>
#include "grammar.h"
#include "bbnf_parser.h"

FILE *bbnfFile; // input file
static int lineNo = 1;
static int columnNo = 1;

// Create new token
static SYMBOL makeToken(std::string lexVal, int tokenType) {
    SYMBOL token;
    token.str = lexVal;
    token.type = tokenType;
    token.lineNo = lineNo;
    token.columnNo = columnNo - lexVal.length() - 1;
    return token;
}

// Lexer: read characters from file and convert into tokens
static SYMBOL getToken() {
    char currentChar, nextChar;
    std::string currentStr = ""; // holds string to be tokenised

    // Skip whitespace
    while (isspace(currentChar = fgetc(bbnfFile))) {
        columnNo++;
        if ((currentChar == '\n') || (currentChar == '\r')) {
            lineNo++;
            columnNo = 1; // start new line after newline character
        }
    }

    // String literal token
    if (currentChar == '"') {
        columnNo++; // discard opening "

        // Add characters to string until closing " reached
        while ((currentChar = fgetc(bbnfFile)) != '"') {
            if (currentChar == '\\') { // \" escape sequence for " in string
                if ((nextChar = fgetc(bbnfFile)) == '"') {
                    columnNo++;
                    currentChar = nextChar; // skip \ in currentStr
                } else {
                    fseek(bbnfFile, -1, SEEK_CUR); // don't lose next character, move back 1
                }
            }
            currentStr += currentChar;
            columnNo++;
        }

        columnNo++; // discard closing "
        if (currentStr == "")
            return makeToken(currentStr, EPSILON); // epsilon = empty string
        return makeToken(currentStr, LITERAL);
    }

    // Add characters to string until non-underscore/alphanumeric character reached
    while (isalnum(currentChar) || (currentChar == '_')) {
        currentStr += currentChar;
        columnNo++;
        currentChar = fgetc(bbnfFile);
    }

    // If characters have been added to string, return non-terminal or epsilon token
    if (currentStr != "") {
        fseek(bbnfFile, -1, SEEK_CUR); // don't lose current character, move back 1
        if (currentStr == "epsilon")
            return makeToken("", EPSILON);
        return makeToken(currentStr, NON_TERM);
    }

    // After -, check for > to build -> derivation symbol token
    if (currentChar == '-') {
        if ((nextChar = fgetc(bbnfFile)) == '>') {
            columnNo += 2;
            return makeToken("->", DERIVE);
        } else {
            fseek(bbnfFile, -1, SEEK_CUR);  // don't lose next character, move back 1
            columnNo++;
            return makeToken("-", INVALID); // - without > is invalid
        }
    }

    // Single character tokens
    columnNo++;
    switch (currentChar) {
        case '|':
            return makeToken("|", DISJ);
        case '&':
            return makeToken("&", CONJ);
        case '~':
            return makeToken("~", NEG);
        case ';':
            return makeToken(";", SC);
        case EOF:
            return makeToken("EOF", EOF_CHAR);
    }

    // If current character not recognised, create invalid token
    std::string s(1, currentChar);
    return makeToken(s, INVALID);
}

static SYMBOL currentToken; // token that parser is currently reading

// Parser error: show incorrect (current) token, its position, and expected sequence
static void parseError(std::string expected) {
    std::cout << "Parse error [ln " + std::to_string(currentToken.lineNo) + ", col " + std::to_string(currentToken.columnNo) + "]: unexpected token '" + currentToken.str + "' (expecting " + expected + ")\n";
    exit(1); // quit on error
}

// Check if current token is of given type
static bool match(int tokType) {
    if (currentToken.type == tokType) {
        currentToken = getToken(); // if matched, move on to next token
        return true;
    }
    return false; // do not move on, current token will be checked again
}

// Parse symbol (non-terminal, string literal, or epsilon)
StrSet alphabet; // terminals used by grammar
static SYMBOL parseSymbol() {
    SYMBOL symb = currentToken;
    if (!match(NON_TERM) && !match(LITERAL) && !match(EPSILON))
        parseError("non-terminal or literal");

    // Record symbol for alphabet if terminal
    if (symb.type != NON_TERM)
        alphabet.insert(symb.str);
    return symb;
}

// Parse conjunct: sequence of symbols, may be negated
static GNode parseConj() {
    bool pos = true; // assume conjunct is positive
    if (match(NEG))
        pos = false; // if starts with '~', conjunct is negative

    // Add symbol to sequence until ampersand, pipe or semicolon reached
    SymbVec symbols;
    do {
        SYMBOL nextSymb = parseSymbol();
        symbols.push_back(nextSymb);
    } while ((currentToken.type != CONJ) && (currentToken.type != DISJ) && (currentToken.type != SC));
    return std::make_shared<Conjunct>(std::move(symbols), pos);
}

// Parse rule: list of conjuncts
static GNode parseRule() {
    GNodeList conjList;

    // Add conjunct to list until conjunct not followed by ampersand
    do {
        conjList.push_back(parseConj());
    } while (match(CONJ));
    return std::make_shared<Rule>(std::move(conjList));
}

// Parse disjunction of rules
static GNode parseDisj() {
    GNodeList ruleList;

    // Add rule to list until rule not followed by pipe
    do {
        ruleList.push_back(parseRule());
    } while (match(DISJ));

    // Disjunction must be terminated with semicolon
    if (!match(SC))
        parseError("';'");
    return std::make_shared<Disj>(std::move(ruleList));
}

// Parse grammar: map non-terminals to disjunctions of rules
std::map<std::string, GNode> parseGrammar() {
    std::map<std::string, GNode> disjList;
    currentToken = getToken(); // get first token

    // Add disjunction until EOF reached
    do {
        std::string nt = currentToken.str; // get key (non-terminal)
        if (!match(NON_TERM))
            parseError("non-terminal");
        if (!match(DERIVE))
            parseError("'->'"); // non-terminal must be followed by derive symbol

        GNode nextDisj = parseDisj();       // get value (disjunction)
        disjList[nt] = std::move(nextDisj); // insert key & value into map
    } while (!match(EOF_CHAR));
    return disjList;
}
