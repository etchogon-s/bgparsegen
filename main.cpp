#include <algorithm>
#include <iostream>
#include "grammar.h"
#include "bbnf_parser.h"
#include "rd_codegen.h"

//---------------------//
// Grammar AST Printer //
//---------------------//

// Print elements of set of strings
std::string strSetString(StrSet strs) {
    std::string result = "";
    for (const std::string& s : strs) {
        result += " ";
        if (s == "")
            result += "epsilon"; // represents empty string
        else
            result += s;
    }
    return result;
}

// Make indentation of given width
std::string makeIndent(int depth) {
    std::string indent = "";
    while (depth > 0) {
        indent += "    ";
        depth--;
    }
    return indent;
}

// Convert GNodeList to string
std::string nlString(const GNodeList& list, int depth) {
    std::string result = "";
    for (const GNode& n : list) {
        if (n != nullptr)
            result += n->toString(depth); // convert each item and add to result string
    }
    return result;
}

// Print symbol
std::string printSymb(const SYMBOL& symbol, int depth) {
    std::string result = makeIndent(depth);
    if (symbol.type == NON_TERM)
        result += "NON-";
    result += "TERMINAL: ";
    if (symbol.str == "")
        result += "epsilon"; // represents empty string
    else
        result += symbol.str;
    return result + "\n";
}

// Print conjunct (show whether positive or negative, and print sequence of symbols)
std::string Conjunct::toString(int depth) const {
    std::string result = makeIndent(depth);
    if (Pos)
        result += "+VE";
    else
        result += "-VE";
    result += " CONJUNCT:\n";
    for (const SYMBOL& symb : Symbols)
        result += printSymb(symb, depth + 1);
    return result;
}

// Print rule (series of conjuncts)
std::string Rule::toString(int depth) const {
    return makeIndent(depth) + "RULE:\n" + nlString(ConjList, depth + 1);
}

// Print disjunction (series of rules)
std::string Disj::toString(int depth) const {
    return makeIndent(depth) + nlString(RuleList, depth + 1);
}

//----------------------------------------------//
// Sort Non-Terminals for FIRST Set Computation //
//----------------------------------------------//

// Get set of non-terminals used in conjunct
StrSet Conjunct::references() const {
    StrSet ntsReferenced;
    for (const SYMBOL& symb : Symbols) {
        if (symb.type == NON_TERM)
            ntsReferenced.insert(symb.str);
    }
    return ntsReferenced;
}

// Get set of non-terminals used in rule (union of conjuncts' sets of non-terminals)
StrSet Rule::references() const {
    StrSet ntsReferenced;
    for (const GNode& conj : ConjList) {
        auto conjReferences = conj->references();
        ntsReferenced.insert(conjReferences.cbegin(), conjReferences.cend());
    }
    return ntsReferenced;
}

// Get set of non-terminals used in disjunction (union of rules' sets of non-terminals)
StrSet Disj::references() const {
    StrSet ntsReferenced;
    for (const GNode& rule : RuleList) {
        auto ruleReferences = rule->references();
        ntsReferenced.insert(ruleReferences.cbegin(), ruleReferences.cend());
    }
    return ntsReferenced;
}

// Depth-first search on adjacency list
StrSet visited;
StrVec dfs(std::string nt, StrVec ntOrder, std::map<std::string, StrSet> ntRefs) {
    visited.insert(nt); // mark non-terminal as visited

    // Depth-first search on all non-terminals referenced by nt that are unvisited
    for (const std::string& s : ntRefs[nt]) {
        if (visited.count(s) == 0)
            ntOrder = dfs(s, ntOrder, ntRefs);
    }
    ntOrder.push_back(nt); // add nt to ordering
    return ntOrder;
}

// Topological sort for non-terminals
StrVec topologicalSort(std::map<std::string, StrSet> ntRefs) {
    StrVec ntOrder;
    
    // Start depth-first search
    for (const auto& nt : ntRefs) {
        if (visited.count(nt.first) == 0)
            ntOrder = dfs(nt.first, ntOrder, ntRefs);
    }
    return ntOrder; // return topological ordering
}

//--------------------//
// Compute FIRST Sets //
//--------------------//

std::map<std::string, StrSet> firstSets; // maps each non-terminal to its FIRST set

// Compute FIRST set of conjunct
StrSet Conjunct::firstSet() {
    if (!Pos)
        return alphabet; // if conjunct is negative, FIRST set is FIRST(alphabet*)

    // Add to FIRST set until a non-nullable symbol is reached in the conjunct
    StrSet firsts;
    for (const SYMBOL& symb : Symbols) {
        /* If first symbol in the conjunct is epsilon, this is the conjunct's only symbol
         * The conjunct's FIRST set contains epsilon only */
        if (symb.type == EPSILON) {
            firsts.insert(""); // epsilon = empty string
            return firsts;

        // Terminal is non-nullable, so FIRST set is complete after adding it
        } else if (symb.type == LITERAL) {
            firsts.insert(symb.str);
            Nullable = false;
            return firsts;

        // If s is a non-terminal, add elements of its FIRST set to conjunct's FIRST set
        } else {
            StrSet symbFirsts = firstSets[symb.str];
            firsts.insert(symbFirsts.cbegin(), symbFirsts.cend());

            // s is non-nullable if its FIRST set does not contain empty string
            if (!symbFirsts.contains("")) {
                Nullable = false;
                return firsts; // conjunct FIRST set is complete when non-nullable reached
            }
        }
    }
    return firsts; // all symbols in the conjunct are nullable
}

// Compute FIRST set of rule (intersection of conjuncts' FIRST sets)
StrSet Rule::firstSet() {
    Firsts = alphabet; // start with entire alphabet

    // For each conjunct, remove items that are not in the conjunct's FIRST set
    for (const GNode& conj : ConjList) {
        StrSet conjFirsts = conj->firstSet();
        for (auto it = Firsts.begin(); it != Firsts.end();) {
            if (!conjFirsts.contains(*it))
                it = Firsts.erase(it);
            else
                it++;
        }
    }
    return Firsts;
}

// Compute FIRST set of disjunction (union of rules' FIRST sets)
StrSet Disj::firstSet() {
    StrSet firsts;
    for (const GNode& rule : RuleList) {
        StrSet ruleFirsts = rule->firstSet();
        firsts.insert(ruleFirsts.cbegin(), ruleFirsts.cend());
    }
    return firsts;
}

//---------------------//
// Compute FOLLOW Sets //
//---------------------//

std::map<std::string, StrSet> followSets; // maps each non-terminal to its FOLLOW set

// Build FOLLOW sets of non-terminals used in conjunct
void Conjunct::followAdd(std::string nt) const {
    size_t conjSize = Symbols.size();
    size_t nextIndex;
    bool nonNullableFound;

    // Iterate over symbols in conjunct to find non-terminals
    for (size_t i = 0; i < conjSize; i++) {
        const SYMBOL& current = Symbols[i];

        // If symbol is non-terminal, look at subsequent symbols to add to its FOLLOW set
        if (current.type == NON_TERM) {
            nextIndex = i + 1;
            nonNullableFound = false;

            // If current symbol's FOLLOW set doesn't exist yet, create it
            if (followSets.count(current.str) == 0)
                followSets[current.str] = StrSet();

            // Add to FOLLOW set until non-nullable symbol or end of conjunct reached
            while (!nonNullableFound && (nextIndex < conjSize)) {
                const SYMBOL& next = Symbols[nextIndex];
                if (next.type == LITERAL) {
                    followSets[current.str].insert(next.str); // put terminal in FOLLOW set
                    nonNullableFound = true;                  // terminal is non-nullable

                // If non-terminal, add elements of its FIRST set to current's FOLLOW set
                } else if (next.type == NON_TERM) {
                    StrSet nextFirsts = firstSets[next.str];
                    followSets[current.str].insert(nextFirsts.cbegin(), nextFirsts.cend());

                    // Non-nullable if FIRST set does not contain empty string
                    if (!nextFirsts.contains(""))
                        nonNullableFound = true;
                }
                nextIndex++; // go to next symbol
            }

            /* If all the symbols after the current symbol are nullable, add elements of the
             * FOLLOW set of the deriving non-terminal (if it is different from current) to
             * current's FOLLOW set */
            if (!nonNullableFound && (nt != current.str)) {
                StrSet& ntFollowing = followSets[nt];
                followSets[current.str].insert(ntFollowing.cbegin(), ntFollowing.cend());
            }
        }
    }
    return;
}

// Build FOLLOW sets of non-terminals used in rule (for each conjunct, add to sets)
void Rule::followAdd(std::string nt) const {
    for (const GNode& conj : ConjList)
        conj->followAdd(nt);
    return;
}

// Build FOLLOW sets of non-terminals used in disjunction (for each rule, add to sets)
void Disj::followAdd(std::string nt) const {
    for (const GNode& rule : RuleList)
        rule->followAdd(nt);
    return;
}

//-----------------------//
// Compute Parsing Table //
//-----------------------//

// Parsing table, maps pair of non-terminal and string to rule (list of conjuncts)
std::map<std::pair<std::string, std::string>, GNodeList> parseTable;

// Add rule to parsing table entry for non-terminal nt and string s if suitable
void Rule::updateTable(std::string nt) {
    // If any conjunct is not nullable, rule is not nullable
    size_t i = 0;
    while (Nullable && (i < ConjList.size())) {
        Nullable = ConjList[i]->Nullable && Nullable;
        i++;
    }

    /* If string s is in rule's FIRST set, OR rule is nullable and s is in nt's FOLLOW set,
     * add rule to entry (nt, s) of parsing table */
    for (const std::string& s : alphabet) {
        if (Firsts.contains(s) || (Nullable && followSets[nt].contains(s))) {
            std::pair<std::string, std::string> tableEntry = make_pair(nt, s);
            GNodeList entryContent;
            for (const GNode& conj : ConjList)
                entryContent.push_back(conj);
            parseTable[tableEntry] = entryContent;
        }
    }
    return;
}

// Build parsing table by adding each rule in disjunction to any suitable entries
void Disj::updateTable(std::string nt) {
    for (const GNode& rule : RuleList)
        rule->updateTable(nt);
    return;
}

//-------------//
// Main Driver //
//-------------//

// Get input file and user's algorithm choice
int main(int argc, char **argv) {
    if (argc == 3) {
        bbnfFile = fopen(argv[1], "r");
        if (bbnfFile == NULL) {
            std::cout << "Error opening file\n";
            return 1;
        }
    } else {
        std::cout << "Usage: ./code <input file> <algorithm>\n";
        return 1;
    }

    // Parse input file
    std::map<std::string, GNode> grammar = parseGrammar();
    fclose(bbnfFile);
    std::cout << "Alphabet:" + strSetString(alphabet) + "\n"; // print alphabet
    alphabet.insert("");

    // Print grammar AST
    std::cout << "\nGrammar AST\n";
    for (const auto& disj : grammar)
        std::cout << "TERMINAL " + disj.first + "\n" + disj.second->toString(0);

    /* Build adjacency list: map each non-terminal to set of non-terminals used in rules
     * derived from it */
    std::cout << "\nReferenced Non-Terminals\n";
    std::map<std::string, StrSet> ntRefs;
    for (const auto& disj : grammar) {
        std::string nt = disj.first;
        ntRefs[nt] = disj.second->references();
        std::cout << nt + ":" + strSetString(ntRefs[nt]) + "\n"; // print mappings
    }

    // Compute topological ordering of non-terminals and print non-terminals in this order
    StrVec ntOrder = topologicalSort(ntRefs);
    std::cout << "\nOrder of Computing FIRST Sets:";
    for (const std::string& s : ntOrder)
        std::cout << " " + s;
    std::cout << "\n";

    // Compute and print FIRST sets of non-terminals (in topological order)
    std::cout << "\nFIRST Sets\n";
    for (const std::string& s : ntOrder) {
        firstSets[s] = grammar[s]->firstSet();
        std::cout << s + ":" + strSetString(firstSets[s]) + "\n";
    }

    // Compute FOLLOW sets of non-terminals
    std::reverse(ntOrder.begin(), ntOrder.end()); // reverse order of non-terminals
    for (size_t i = 0; i < ntOrder.size(); i++) {
        const std::string& s = ntOrder[i];
        if (i == 0) { // first symbol in ordering is start symbol
            followSets[s] = StrSet();
            followSets[s].insert(""); // FOLLOW set of start symbol is just epsilon
        }
        grammar[s]->followAdd(s);
    }

    // Print FOLLOW sets
    std::cout << "\nFOLLOW Sets\n";
    for (const std::string& s : ntOrder)
        std::cout << s + ":" + strSetString(followSets[s]) + "\n";

    // Build parsing table
    for (const auto& disj : grammar)
        disj.second->updateTable(disj.first);

    // Print parsing table
    std::cout << "\nParsing Table\n";
    for (const auto& entry : parseTable) {
        std::cout << "NON-TERMINAL " + (entry.first).first + ", STRING ";
        if ((entry.first).second == "")
            std::cout << "epsilon";
        else
            std::cout << (entry.first).second;
        std::cout << "\n" + makeIndent(1) + "RULE:\n" + nlString(entry.second, 2);
    }

    // Generate recursive descent parser code
    std::reverse(ntOrder.begin(), ntOrder.end()); // reverse order of non-terminals
    RDCodegen(ntOrder);
    return 0;
}
