#pragma once
#include <vector>
#include <memory>
#include <string>

class IRNode;
class IRBasicBlock;
class IRGraph;

enum class IRNodeKind {
    Constant,
    Parameter,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,
    Load,
    Store,
    Phi,
    Compare,
    Branch,
    Jump,
    Return,
    Call,
    Delete,
    Merge,

    NewList,
    ListSet
};

class IRNode {
public:
    IRNodeKind kind;
    int id;
    std::string value;
    std::vector<IRNode*> inputs;
    std::vector<IRNode*> uses;

    IRNode(IRNodeKind k, int id)
        : kind(k), id(id) {}
};

class IRBasicBlock {
public:
    int id;
    std::vector<IRNode*> nodes;
    IRNode* terminator = nullptr;
    std::vector<IRBasicBlock*> preds;
    std::vector<IRBasicBlock*> succs;

    IRBasicBlock(int id)
        : id(id) {}
};

class IRGraph {
public:
    int nextNodeId = 0;
    int nextBlockId = 0;

    std::vector<std::unique_ptr<IRNode>> nodes;
    std::vector<std::unique_ptr<IRBasicBlock>> blocks;

    IRBasicBlock* entry = nullptr;

    IRGraph() {
        entry = createBlock();
    }

    IRBasicBlock* createBlock() {
        auto block = std::make_unique<IRBasicBlock>(nextBlockId++);
        IRBasicBlock* ptr = block.get();
        blocks.push_back(std::move(block));
        return ptr;
    }

    IRNode* createNode(IRNodeKind kind) {
        auto node = std::make_unique<IRNode>(kind, nextNodeId++);
        IRNode* ptr = node.get();
        nodes.push_back(std::move(node));
        return ptr;
    }

    IRNode* createConstant(const std::string& value) {
        IRNode* n = createNode(IRNodeKind::Constant);
        n->value = value;
        return n;
    }

    IRNode* addInput(IRNode* node, IRNode* input) {
        node->inputs.push_back(input);
        input->uses.push_back(node);
        return node;
    }

    IRNode* makePhi(const std::vector<IRNode*>& inputs) {
        IRNode* n = createNode(IRNodeKind::Phi);
        for (auto* in : inputs)
            addInput(n, in);
        return n;
    }

    IRNode* makeBranch(IRNode* cond, IRBasicBlock* t, IRBasicBlock* f) {
        IRNode* n = createNode(IRNodeKind::Branch);
        addInput(n, cond);
        t->preds.push_back(nullptr);
        f->preds.push_back(nullptr);
        return n;
    }

    IRNode* makeJump(IRBasicBlock* target) {
        IRNode* n = createNode(IRNodeKind::Jump);
        target->preds.push_back(nullptr);
        return n;
    }

    IRNode* makeReturn(IRNode* value) {
        IRNode* n = createNode(IRNodeKind::Return);
        addInput(n, value);
        return n;
    }

    IRNode* makeNewList(int count) {
        IRNode* n = createNode(IRNodeKind::NewList);
        n->value = std::to_string(count);
        return n;
    }

    IRNode* makeListSet(IRNode* list, IRNode* index, IRNode* value) {
        IRNode* n = createNode(IRNodeKind::ListSet);
        addInput(n, list);
        addInput(n, index);
        addInput(n, value);
        return n;
    }
};
