#include <algorithm>
#include <iostream>
#include <stdlib.h>
#include "grammar.h"
#include "input_parser.h"
//#include "rd_codegen.h"

//---------------------//
// Grammar AST Printer //
//---------------------//

// Print elements of set of strings
std::string printStrs(StrSet strs) {
    std::string result = "";
    for (const std::string& s : strs)
        result = (s == "") ? result + " EPSILON," : result + " " + s + ",";
    result.pop_back();
    return result;
}

// Print elements of vector of strings
std::string printStrs(StrVec strs) {
    std::string result = "";
    for (const std::string& s : strs)
        result = (s == "") ? result + " EPSILON," : result + " " + s + ",";
    result.pop_back();
    return result;
}

// Print elements of set of vectors of strings
std::string printStrs(std::set<StrVec> fSet) {
    std::string result = "";
    for (StrVec v : fSet) {
        for (std::string s : v)
            result = (s == "") ? result + " EPSILON" : result + " " + s;
        result += ",";
    }
    result.pop_back();
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
    switch (symbol.type) {
        case EPSILON:
            return result + "EPSILON\n";
        case NON_TERM:
            result += "NON-";
            break;
    }

    return result + "TERMINAL: " + symbol.str + "\n";
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

//-----------------------------------------------------//
// Sort Non-Terminals for FIRST/FOLLOW Set Computation //
//-----------------------------------------------------//

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

/* Concatenate each sequence in set "seqs" with each sequence in set "addSeqs"
 * Truncate each resulting sequence to k symbols, and add it to new set
 * Return this new set */
std::set<StrVec> allConcat(std::set<StrVec> seqs, std::set<StrVec> addSeqs, int k) {
    if (seqs.empty())
        return addSeqs;

    std::set<StrVec> newSeqs;
    for (StrVec v : seqs) {
        for (StrVec vec : addSeqs) {
            StrVec newV = v;
            newV.erase(std::remove(newV.begin(), newV.end(), ""), newV.end());
            int i = 0;
            while ((newV.size() < k) && (i < vec.size())) {
                if (vec[i] != "")
                    newV.push_back(vec[i]);
                i++;
            }
            if (newV.empty())
                newV.push_back("");
            newSeqs.insert(newV);
        }
    }

    return newSeqs;
}

// FIRST/FOLLOW set of each non-terminal (key) is a set of sequences of terminals (value)
std::map<std::string, std::set<StrVec>> firstSets, followSets;

//--------------------//
// Compute FIRST Sets //
//--------------------//

// Compute FIRST set of conjunct
std::set<StrVec> Conjunct::firstSet(std::string nt, int k) {
    std::set<StrVec> firsts = std::set<StrVec>(); // conjunct FIRST set
    if (!Pos)
        return firsts; // if conjunct is negative, return empty set

    bool nullable = true;
    for (const SYMBOL& symb : Symbols) {
        if (symb.type == LITERAL) {
            nullable = false; // terminal is non-nullable, so conjunct is non-nullable

            // Append terminal to each sequence in conjunct FIRST set with length < k
            std::set<StrVec> symbSeq;
            symbSeq.insert({symb.str});
            firsts = allConcat(firsts, symbSeq, k);

        } else if (symb.type == NON_TERM) {
            /* If deriving non-terminal is used in conjunct, it must be nullable
             * Add epsilon & elements of conjunct FIRST set to its FIRST set */
            if (symb.str == nt) {
                firstSets[symb.str].insert(firsts.cbegin(), firsts.cend());
                firstSets[symb.str].insert({""});
            } else {
                if (!firstSets[symb.str].contains({""}))
                    nullable = false; // if non-terminal is non-nullable, so is conjunct
            }

            /* Concatenate each sequence in conjunct FIRST set with each sequence in non-
             * terminal's FIRST set
             * Add sequence consisting of first k symbols of result to a new set
             * Replace conjunct FIRST set with this new set */
            firsts = allConcat(firsts, firstSets[symb.str], k);
        }
    }

    // If conjunct is nullable, FIRST set of conjunct contains epsilon
    if (nullable)
        firsts.insert({""});
    return firsts;
}

// Compute FIRST set of rule (intersection of conjuncts' FIRST sets)
std::set<StrVec> Rule::firstSet(std::string nt, int k) {
    Firsts = std::set<StrVec>(); // rule FIRST set

    for (const GNode& conj : ConjList) {
        std::set<StrVec> conjFirsts = conj->firstSet(nt, k); // get FIRST set of conjunct
        if (!conjFirsts.empty()) {
            if (Firsts.empty())
                Firsts = conjFirsts;

            // Remove items that are not in current conjunct FIRST set
            for (auto it = Firsts.begin(); it != Firsts.end();) {
                if (!conjFirsts.contains(*it))
                    it = Firsts.erase(it);
                else
                    it++;
            }
        }
    }
    return Firsts;
}

// Compute FIRST set of disjunction (union of rules' FIRST sets)
std::set<StrVec> Disj::firstSet(std::string nt, int k) {
    std::set<StrVec> firsts;

    // Add elements of each rule's FIRST set to disjunction FIRST set
    for (const GNode& rule : RuleList) {
        std::set<StrVec> ruleFirsts = rule->firstSet(nt, k);
        firsts.insert(ruleFirsts.cbegin(), ruleFirsts.cend());
    }
    return firsts;
}

//---------------------//
// Compute FOLLOW Sets //
//---------------------//

// Build FOLLOW sets of non-terminals used in conjunct
void Conjunct::followAdd(std::string nt, int k) const {
    size_t conjSize = Symbols.size();
    size_t nextIndex;

    // Iterate over symbols in conjunct to find non-terminals
    for (size_t i = 0; i < conjSize; i++) {
        const SYMBOL& current = Symbols[i];

        // If symbol is non-terminal, look at subsequent symbols
        if (current.type == NON_TERM) {
            nextIndex = i + 1;
            std::set<StrVec> partialFollow = std::set<StrVec>();

            // Add to partial FOLLOW set until end of conjunct reached
            while (nextIndex < conjSize) {
                const SYMBOL& next = Symbols[nextIndex];

                // Append terminal to each sequence in partial FOLLOW set with length < k
                if (next.type == LITERAL) {
                    std::set<StrVec> nextSeq;
                    nextSeq.insert({next.str});
                    partialFollow = allConcat(partialFollow, nextSeq, k);

                /* Concatenate each sequence in partial FOLLOW set with each sequence in
                 * non-terminal's FIRST set
                 * Add sequence consisting of first k symbols of result to a new set
                 * Replace partial FOLLOW set with this new set */
                } else if (next.type == NON_TERM) {
                    partialFollow = allConcat(partialFollow, firstSets[next.str], k);
                }

                nextIndex++; // go to next symbol
            }

            /* When end of conjunct is reached, concatenate each sequence in FOLLOW set of
             * the deriving non-terminal with each sequence in current's FOLLOW set
             * Add sequence consisting of first k symbols of result to a new set
             * Replace FOLLOW set with this new set */
            std::string cStr = current.str;
            partialFollow = allConcat(partialFollow, followSets[nt], k);

            // Merge partial FOLLOW set with current's FOLLOW set
            if (followSets.count(cStr) == 0)
                followSets[cStr] = std::set<StrVec>();
            followSets[cStr].insert(partialFollow.cbegin(), partialFollow.cend());
        }
    }
    return;
}

// Build FOLLOW sets of non-terminals used in rule (for each conjunct, add to sets)
void Rule::followAdd(std::string nt, int k) const {
    for (const GNode& conj : ConjList)
        conj->followAdd(nt, k);
    return;
}

// Build FOLLOW sets of non-terminals used in disjunction (for each rule, add to sets)
void Disj::followAdd(std::string nt, int k) const {
    for (const GNode& rule : RuleList)
        rule->followAdd(nt, k);
    return;
}

//-----------------------//
// Compute Parsing Table //
//-----------------------//

// Parsing table, maps pair of non-terminal and sequence to rule (list of conjuncts)
std::map<std::pair<std::string, StrVec>, GNodeList> parseTable;

// Update parsing table by adding the given rule to entries
void Rule::updateTable(std::string nt, int k) {

    /* All possible terminal sequences to which this rule could be applied:
     * Concatenate each sequence in rule's FIRST set with each sequence in nt's FOLLOW set
     * Truncate each resulting sequence to k symbols, and add it to set */
    std::set<StrVec> sequences = allConcat(Firsts, followSets[nt], k);

    // For each sequence, add the rule to the parsing table entry for nt and this sequence
    for (StrVec v : sequences) {
        std::pair<std::string, StrVec> tableEntry = make_pair(nt, v);
        GNodeList entryRule;
        for (const GNode& conj : ConjList)
            entryRule.push_back(conj);
        parseTable[tableEntry] = entryRule;
    }
    return;
}

// Build parsing table by adding each rule in disjunction to any suitable entries
void Disj::updateTable(std::string nt, int k) {
    for (const GNode& rule : RuleList)
        rule->updateTable(nt, k);
    return;
}

//-------------//
// Main Driver //
//-------------//

int main(int argc, char **argv) {
    int k;
    if (argc == 3) {
        inpFile = fopen(argv[1], "r"); // get input file
        if (inpFile == NULL) {
            std::cout << "Error opening file\n";
            return 1;
        }
        k = atoi(argv[2]); // get value of k
        if (k < 1) {
            std::cout << "k cannot be less than 1\n";
            return 1;
        }
    } else {
        std::cout << "Usage: ./code <input file> <k>\n";
        return 1;
    }

    // Parse input file
    std::map<std::string, GNode> grammar = parseGrammar();
    fclose(inpFile);

    // Print grammar AST
    std::cout << "Grammar AST\n";
    for (const auto& disj : grammar)
        std::cout << "NON-TERMINAL " + disj.first + "\n" + disj.second->toString(0);

    /* Build adjacency list: map each non-terminal to set of non-terminals used in rules
     * derived from it */
    std::map<std::string, StrSet> ntRefs;
    for (const auto& disj : grammar) {
        std::string nt = disj.first;
        ntRefs[nt] = disj.second->references();
    }
    StrVec ntOrder = topologicalSort(ntRefs); // compute topological ordering

    // Compute FIRST sets of non-terminals, in topological order
    for (const std::string& s : ntOrder)
        firstSets[s] = grammar[s]->firstSet(s, k);

    // Compute FOLLOW sets of non-terminals
    std::reverse(ntOrder.begin(), ntOrder.end()); // reverse order of non-terminals
    for (size_t i = 0; i < ntOrder.size(); i++) {
        const std::string& s = ntOrder[i];
        if (i == 0) { // first symbol in ordering is start symbol
            followSets[s] = std::set<StrVec>();
            followSets[s].insert({""}); // FOLLOW set of start symbol is just epsilon
        }
        grammar[s]->followAdd(s, k);
    }

    // Print FIRST and FOLLOW sets
    std::cout << "\nFIRST Sets\n";
    for (const std::string& s : ntOrder)
        std::cout << s + ":" + printStrs(firstSets[s]) + "\n";
    std::cout << "\nFOLLOW Sets\n";
    for (const std::string& s : ntOrder)
        std::cout << s + ":" + printStrs(followSets[s]) + "\n";

    // Build parsing table
    for (const auto& disj : grammar)
        disj.second->updateTable(disj.first, k);

    // Print parsing table
    std::cout << "\nLL(" + std::to_string(k) + ") Parsing Table\n";
    for (const auto& entry : parseTable) {
        std::string entryStr = "NON-TERMINAL " + (entry.first).first + ", SEQUENCE";
        entryStr += printStrs((entry.first).second) + "\n";
        std::cout << entryStr + makeIndent(1) + "RULE:\n" + nlString(entry.second, 2);
    }

    // Generate recursive descent parser code
    //std::reverse(ntOrder.begin(), ntOrder.end()); // reverse order of non-terminals
    //RDCodegen(ntOrder);
    return 0;
}
