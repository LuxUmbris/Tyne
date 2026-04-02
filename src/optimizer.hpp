#pragma once
#include "son_ir.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <algorithm>
#include <string>

class Optimizer {

    class CSEPass {
        struct ExprKey {
            IRNodeKind kind;
            std::string value;
            std::vector<int> inputs;

            bool operator==(const ExprKey& o) const {
                return kind == o.kind && value == o.value && inputs == o.inputs;
            }
        };

        struct Hash {
            std::size_t operator()(const ExprKey& k) const {
                std::size_t h = std::hash<int>()(static_cast<int>(k.kind));
                h ^= std::hash<std::string>()(k.value) + 0x9e3779b9 + (h << 6) + (h >> 2);
                for (int id : k.inputs)
                    h ^= std::hash<int>()(id) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

        static bool isPure(IRNodeKind k) {
            switch (k) {
                case IRNodeKind::Add:
                case IRNodeKind::Sub:
                case IRNodeKind::Mul:
                case IRNodeKind::Div:
                case IRNodeKind::Mod:
                case IRNodeKind::Neg:
                case IRNodeKind::Compare:
                case IRNodeKind::Constant:
                case IRNodeKind::Parameter:
                case IRNodeKind::Phi:
                    return true;
                default:
                    return false;
            }
        }

        static ExprKey makeKey(IRNode* n) {
            ExprKey k;
            k.kind = n->kind;
            k.value = n->value;
            for (IRNode* in : n->inputs)
                k.inputs.push_back(in->id);
            return k;
        }

        static void replaceNode(IRNode* oldNode, IRNode* newNode) {
            for (IRNode* use : oldNode->uses) {
                for (IRNode*& in : use->inputs)
                    if (in == oldNode)
                        in = newNode;
                newNode->uses.push_back(use);
            }
            oldNode->uses.clear();
        }

    public:
        void run(IRGraph& graph) {
            std::unordered_map<ExprKey, IRNode*, Hash> table;

            for (auto& uptr : graph.nodes) {
                IRNode* n = uptr.get();
                if (!isPure(n->kind))
                    continue;

                ExprKey key = makeKey(n);
                auto it = table.find(key);
                if (it == table.end()) {
                    table.emplace(key, n);
                } else {
                    IRNode* existing = it->second;
                    replaceNode(n, existing);

                    for (auto& bptr : graph.blocks) {
                        auto& vec = bptr->nodes;
                        vec.erase(std::remove(vec.begin(), vec.end(), n), vec.end());
                    }
                }
            }
        }
    };

    class DCEPass {
        static bool isSideEffecting(IRNodeKind k) {
            switch (k) {
                case IRNodeKind::Store:
                case IRNodeKind::Call:
                case IRNodeKind::Branch:
                case IRNodeKind::Jump:
                case IRNodeKind::Return:
                case IRNodeKind::Merge:
                case IRNodeKind::Delete:
                    return true;
                default:
                    return false;
            }
        }

        void mark(IRNode* n, std::vector<IRNode*>& wl) {
            if (!isSideEffecting(n->kind) && n->uses.empty())
                wl.push_back(n);
        }

        void removeNode(IRGraph& graph, IRNode* n, std::vector<IRNode*>& wl) {
            for (IRNode* in : n->inputs) {
                auto& uses = in->uses;
                uses.erase(std::remove(uses.begin(), uses.end(), n), uses.end());
                mark(in, wl);
            }
            n->inputs.clear();

            for (auto& bptr : graph.blocks) {
                auto& vec = bptr->nodes;
                vec.erase(std::remove(vec.begin(), vec.end(), n), vec.end());
            }
        }

    public:
        void run(IRGraph& graph) {
            std::vector<IRNode*> wl;

            for (auto& uptr : graph.nodes)
                mark(uptr.get(), wl);

            while (!wl.empty()) {
                IRNode* n = wl.back();
                wl.pop_back();
                if (!n->uses.empty())
                    continue;
                removeNode(graph, n, wl);
            }
        }
    };

    class SCCPPass {
        static bool isArithmetic(IRNodeKind k) {
            switch (k) {
                case IRNodeKind::Add:
                case IRNodeKind::Sub:
                case IRNodeKind::Mul:
                case IRNodeKind::Div:
                case IRNodeKind::Mod:
                    return true;
                default:
                    return false;
            }
        }

        static bool allConst(IRNode* n) {
            for (IRNode* in : n->inputs)
                if (in->kind != IRNodeKind::Constant)
                    return false;
            return true;
        }

        static std::string eval(IRNodeKind k, const std::vector<std::string>& v) {
            if (v.empty()) return "";
            int acc = std::stoi(v[0]);
            switch (k) {
                case IRNodeKind::Add:
                    for (size_t i = 1; i < v.size(); ++i) acc += std::stoi(v[i]);
                    return std::to_string(acc);
                case IRNodeKind::Sub:
                    for (size_t i = 1; i < v.size(); ++i) acc -= std::stoi(v[i]);
                    return std::to_string(acc);
                case IRNodeKind::Mul:
                    for (size_t i = 1; i < v.size(); ++i) acc *= std::stoi(v[i]);
                    return std::to_string(acc);
                case IRNodeKind::Div:
                    for (size_t i = 1; i < v.size(); ++i) {
                        int d = std::stoi(v[i]);
                        if (d == 0) return "";
                        acc /= d;
                    }
                    return std::to_string(acc);
                case IRNodeKind::Mod:
                    for (size_t i = 1; i < v.size(); ++i) {
                        int d = std::stoi(v[i]);
                        if (d == 0) return "";
                        acc %= d;
                    }
                    return std::to_string(acc);
                default:
                    return "";
            }
        }

    public:
        void run(IRGraph& graph) {
            for (auto& uptr : graph.nodes) {
                IRNode* n = uptr.get();
                if (!isArithmetic(n->kind))
                    continue;
                if (!allConst(n))
                    continue;

                std::vector<std::string> vals;
                for (IRNode* in : n->inputs)
                    vals.push_back(in->value);

                std::string r = eval(n->kind, vals);
                if (r.empty())
                    continue;

                n->kind = IRNodeKind::Constant;
                n->value = r;

                for (IRNode* in : n->inputs) {
                    auto& uses = in->uses;
                    uses.erase(std::remove(uses.begin(), uses.end(), n), uses.end());
                }
                n->inputs.clear();
            }
        }
    };

    class PhiSimplifyPass {
    public:
        void run(IRGraph& graph) {
            for (auto& uptr : graph.nodes) {
                IRNode* n = uptr.get();
                if (n->kind != IRNodeKind::Phi)
                    continue;
                if (n->inputs.empty())
                    continue;

                IRNode* first = n->inputs[0];
                bool allSame = true;
                for (IRNode* in : n->inputs)
                    if (in != first)
                        allSame = false;

                if (!allSame)
                    continue;

                for (IRNode* use : n->uses) {
                    for (IRNode*& in : use->inputs)
                        if (in == n)
                            in = first;
                    first->uses.push_back(use);
                }
                n->uses.clear();

                for (auto& bptr : graph.blocks) {
                    auto& vec = bptr->nodes;
                    vec.erase(std::remove(vec.begin(), vec.end(), n), vec.end());
                }
            }
        }
    };

    class CFGSimplifyPass {
    public:
        void run(IRGraph& graph) {
            (void)graph;
        }
    };

    class GVNPass {
        struct ExprKey {
            IRNodeKind kind;
            std::string value;
            std::vector<int> inputs;

            bool operator==(const ExprKey& o) const {
                return kind == o.kind && value == o.value && inputs == o.inputs;
            }
        };

        struct Hash {
            std::size_t operator()(const ExprKey& k) const {
                std::size_t h = std::hash<int>()(static_cast<int>(k.kind));
                h ^= std::hash<std::string>()(k.value) + 0x9e3779b9 + (h << 6) + (h >> 2);
                for (int id : k.inputs)
                    h ^= std::hash<int>()(id) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

        static bool isCandidate(IRNodeKind k) {
            switch (k) {
                case IRNodeKind::Add:
                case IRNodeKind::Sub:
                case IRNodeKind::Mul:
                case IRNodeKind::Div:
                case IRNodeKind::Mod:
                case IRNodeKind::Neg:
                case IRNodeKind::Compare:
                    return true;
                default:
                    return false;
            }
        }

        static ExprKey makeKey(IRNode* n) {
            ExprKey k;
            k.kind = n->kind;
            k.value = n->value;
            for (IRNode* in : n->inputs)
                k.inputs.push_back(in->id);
            return k;
        }

        static void replaceNode(IRNode* oldNode, IRNode* newNode) {
            for (IRNode* use : oldNode->uses) {
                for (IRNode*& in : use->inputs)
                    if (in == oldNode)
                        in = newNode;
                newNode->uses.push_back(use);
            }
            oldNode->uses.clear();
        }

    public:
        void run(IRGraph& graph) {
            std::unordered_map<ExprKey, IRNode*, Hash> table;

            for (auto& bptr : graph.blocks) {
                IRBasicBlock* b = bptr.get();
                for (IRNode* n : b->nodes) {
                    if (!isCandidate(n->kind))
                        continue;

                    ExprKey key = makeKey(n);
                    auto it = table.find(key);
                    if (it == table.end()) {
                        table.emplace(key, n);
                    } else {
                        IRNode* existing = it->second;
                        replaceNode(n, existing);
                    }
                }
            }

            for (auto& uptr : graph.nodes) {
                IRNode* n = uptr.get();
                if (n->uses.empty() && n->inputs.empty() &&
                    isCandidate(n->kind)) {
                    for (auto& bptr : graph.blocks) {
                        auto& vec = bptr->nodes;
                        vec.erase(std::remove(vec.begin(), vec.end(), n), vec.end());
                    }
                }
            }
        }
    };

    class LICMPass {
        static bool isHoistable(IRNodeKind k) {
            switch (k) {
                case IRNodeKind::Add:
                case IRNodeKind::Sub:
                case IRNodeKind::Mul:
                case IRNodeKind::Div:
                case IRNodeKind::Mod:
                case IRNodeKind::Neg:
                case IRNodeKind::Compare:
                case IRNodeKind::Constant:
                case IRNodeKind::Parameter:
                    return true;
                default:
                    return false;
            }
        }

    public:
        void run(IRGraph& graph) {
            if (graph.blocks.size() < 2)
                return;

            IRBasicBlock* header = graph.blocks.front().get();
            std::unordered_set<IRNode*> headerNodes(header->nodes.begin(), header->nodes.end());

            for (auto& bptr : graph.blocks) {
                IRBasicBlock* b = bptr.get();
                if (b == header)
                    continue;

                std::vector<IRNode*> hoist;
                for (IRNode* n : b->nodes) {
                    if (!isHoistable(n->kind))
                        continue;

                    bool invariant = true;
                    for (IRNode* in : n->inputs) {
                        bool inHeader = headerNodes.count(in) != 0;
                        if (!inHeader && in->kind != IRNodeKind::Constant && in->kind != IRNodeKind::Parameter) {
                            invariant = false;
                            break;
                        }
                    }
                    if (invariant)
                        hoist.push_back(n);
                }

                if (hoist.empty())
                    continue;

                auto& vec = b->nodes;
                for (IRNode* n : hoist) {
                    vec.erase(std::remove(vec.begin(), vec.end(), n), vec.end());
                    header->nodes.push_back(n);
                    headerNodes.insert(n);
                }
            }
        }
    };

public:
    void run(IRGraph& graph) {
        CSEPass().run(graph);
        SCCPPass().run(graph);
        PhiSimplifyPass().run(graph);
        CFGSimplifyPass().run(graph);
        DCEPass().run(graph);
        GVNPass().run(graph);
        LICMPass().run(graph);
    }
};
