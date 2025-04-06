#pragma once
#ifndef GRAMMAR_H
#define GRAMMAR_H

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Types of symbols used in input
enum SYMBOL_TYPE {
    NON_TERM, // non-terminal symbol
    LITERAL,  // terminal (string literal)
    EPSILON,
    DERIVE,   // '->' (derivation)
    DISJ,     // '|' (disjunction)
    CONJ,     // '&' (conjunction)
    NEG,      // '~' (negation)
    SC,       // semicolon
    EOF_CHAR, // end of file
    INVALID,
};

struct SYMBOL {
    std::string str;            // lexeme
    int type, lineNo, columnNo; // type, and position in input
};

using StrSet = std::set<std::string>;
using StrVec = std::vector<std::string>;
using SymbVec = std::vector<SYMBOL>;

// Grammar AST node
class GrammarNode {
    public:
        bool Nullable = true;
        virtual ~GrammarNode() {}
        virtual std::string toString(int depth) const {return "";};
        virtual StrSet references() const {return StrSet();};
        virtual StrSet firstSet() {return StrSet();};
        virtual void followAdd(std::string nt) const {};
        virtual bool isPositive() const {return true;};
        virtual void updateTable(std::string nt) {};
        virtual SymbVec getSymbols() const {return SymbVec();};
};

using GNode = std::shared_ptr<GrammarNode>;
using GNodeList = std::vector<GNode>;

// Conjunct (sequence of symbols)
class Conjunct: public GrammarNode {
    SymbVec Symbols;
    bool Pos;        // true if positive conjunct, false if negative conjunct

    public:
        Conjunct(SymbVec symbols, bool pos): Symbols(std::move(symbols)), Pos(pos) {}
        std::string toString(int depth) const override;
        StrSet references() const override;
        StrSet firstSet() override;
        void followAdd(std::string nt) const override;
        bool isPositive() const override {return Pos;};
        SymbVec getSymbols() const override {return Symbols;};
};

// Rule (intersection of conjuncts)
class Rule: public GrammarNode {
    GNodeList ConjList;
    StrSet Firsts;      // FIRST set of rule

    public:
        Rule(GNodeList conjList): ConjList(std::move(conjList)) {}
        std::string toString(int depth) const override;
        StrSet references() const override;
        StrSet firstSet() override;
        void followAdd(std::string nt) const override;
        void updateTable(std::string nt) override;
};

// Disjunction (union of rules)
class Disj: public GrammarNode {
    GNodeList RuleList;

    public:
        Disj(GNodeList ruleList): RuleList(std::move(ruleList)) {}
        std::string toString(int depth) const override;
        StrSet references() const override;
        StrSet firstSet() override;
        void followAdd(std::string nt) const override;
        void updateTable(std::string nt) override;
};

extern StrSet alphabet; // set of terminal symbols used by grammar
extern std::map<std::pair<std::string, std::string>, GNodeList> parseTable;

#endif
