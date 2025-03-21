#ifndef GRAMMAR_H
#define GRAMMAR_H

#include <memory>
#include <set>
#include <string>
#include <vector>

using StrVec = std::vector<std::string>;
using StrSet = std::set<std::string>;

extern StrSet alphabet; // set of terminal symbols used by grammar

// Non-terminal or terminal in conjunct
struct SYMBOL {
    int type;        // same as symbol's token type (terminal, non-terminal, epsilon)
    std::string str; // actual symbol
};

using SymbVec = std::vector<SYMBOL>;

// Grammar AST node
class GrammarNode {
    public:
        virtual ~GrammarNode() {}
        virtual std::string toString(int depth) const {return "";};
        virtual StrSet references() const {return StrSet();};
        virtual StrSet firstSet() {return StrSet();};
        virtual void followAdd(std::string nt) const {};
        virtual void updateTable(std::string nt) {};
        virtual bool isNullable() const {return true;};
        virtual bool isPositive() const {return true;};
        virtual SymbVec getSymbols() const {return SymbVec();};
};

using GNode = std::shared_ptr<GrammarNode>;
using GNodeList = std::vector<GNode>;

// Conjunct (sequence of symbols)
class Conjunct: public GrammarNode {
    SymbVec Symbols;      // symbols
    bool Pos;             // true if positive conjunct, false if negative
    bool Nullable = true; // whether conjunct is nullable, i.e. all symbols are nullable

    public:
        Conjunct(SymbVec symbols, bool pos): Symbols(std::move(symbols)), Pos(pos) {}
        virtual std::string toString(int depth) const override;
        virtual StrSet references() const override;
        virtual StrSet firstSet() override;
        virtual void followAdd(std::string nt) const override;
        virtual bool isNullable() const override {return Nullable;};
        virtual bool isPositive() const override {return Pos;};
        virtual SymbVec getSymbols() const override {return Symbols;};
};

// Rule (intersection of conjuncts)
class Rule: public GrammarNode {
    GNodeList ConjList;   // conjuncts
    StrSet Firsts;        // FIRST set of rule
    bool Nullable = true; // whether rule is nullable, i.e. all conjuncts are nullable

    public:
        Rule(GNodeList conjList): ConjList(std::move(conjList)) {}
        virtual std::string toString(int depth) const override;
        virtual StrSet references() const override;
        virtual StrSet firstSet() override;
        virtual void followAdd(std::string nt) const override;
        virtual void updateTable(std::string nt) override;
};

// Disjunction (union of rules)
class Disj: public GrammarNode {
    GNodeList RuleList; // rules

    public:
        Disj(GNodeList ruleList): RuleList(std::move(ruleList)) {}
        virtual std::string toString(int depth) const override;
        virtual StrSet references() const override;
        virtual StrSet firstSet() override;
        virtual void followAdd(std::string nt) const override;
        virtual void updateTable(std::string nt) override;
};

#endif
