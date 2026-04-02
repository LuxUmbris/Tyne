#pragma once
#include <unordered_map>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "son_ir.hpp"
#include "parser.hpp"

// Forward-declared so ir_builder can recursively compile imported stdlib files
static bool compileAndBuildIR(const std::string& src_path, IRGraph& graph);

class IRBuilder {
    IRGraph& graph;
    IRBasicBlock* currentBlock;

    std::unordered_map<std::string, IRNode*> env;
    std::unordered_map<std::string, IRNode*> lastUse;

    // stdlib search paths tried in order
    std::vector<std::filesystem::path> libSearchPaths;

public:
    IRBuilder(IRGraph& g, std::vector<std::filesystem::path> libPaths = {})
        : graph(g), currentBlock(g.entry), libSearchPaths(std::move(libPaths)) {}

private:
    void markUse(const std::string& name, IRNode* useNode) {
        auto it = lastUse.find(name);
        if (it != lastUse.end()) {
            IRNode* del = graph.createNode(IRNodeKind::Delete);
            graph.addInput(del, it->second);
            currentBlock->nodes.push_back(del);
        }
        lastUse[name] = useNode;
    }

    IRNode* buildExpr(Parser::ASTNode* node) {
        using NK = Parser::NodeKind;

        switch (node->kind) {

        case NK::NumberLiteral: {
            IRNode* c = graph.createConstant(node->value);
            currentBlock->nodes.push_back(c);
            return c;
        }

        case NK::StringLiteral:
        case NK::RawStringLiteral: {
            IRNode* c = graph.createConstant(node->value);
            currentBlock->nodes.push_back(c);
            return c;
        }

        case NK::Identifier: {
            auto name = node->value;
            auto it = env.find(name);
            if (it == env.end()) {
                // Unknown identifier — create a placeholder constant (e.g. stdlib funcs)
                IRNode* ph = graph.createConstant("0");
                currentBlock->nodes.push_back(ph);
                return ph;
            }
            IRNode* val = it->second;
            markUse(name, val);
            return val;
        }

        case NK::MemberAccess: {
            IRNode* obj = buildExpr(node->children[0].get());
            return obj;
        }

        case NK::BinaryExpr: {
            IRNode* lhs = buildExpr(node->children[0].get());
            IRNode* rhs = buildExpr(node->children[1].get());
            IRNodeKind opKind = IRNodeKind::Add;
            const std::string& op = node->value;
            if      (op == "+")  opKind = IRNodeKind::Add;
            else if (op == "-")  opKind = IRNodeKind::Sub;
            else if (op == "*")  opKind = IRNodeKind::Mul;
            else if (op == "/")  opKind = IRNodeKind::Div;
            else if (op == "%")  opKind = IRNodeKind::Mod;
            else                 opKind = IRNodeKind::Compare; // ==, !=, <, >, <=, >=, &&, ||
            IRNode* binNode = graph.createNode(opKind);
            if (op == "==" || op == "!=" || op == "<" || op == ">" ||
                op == "<=" || op == ">=")
                binNode->value = op;
            graph.addInput(binNode, lhs);
            graph.addInput(binNode, rhs);
            currentBlock->nodes.push_back(binNode);
            return binNode;
        }

        case NK::CallExpr: {
            // Build a Call IR node; args are inputs
            IRNode* callNode = graph.createNode(IRNodeKind::Call);
            callNode->value = node->value; // function name
            for (auto& arg : node->children) {
                IRNode* a = buildExpr(arg.get());
                graph.addInput(callNode, a);
            }
            currentBlock->nodes.push_back(callNode);
            return callNode;
        }

        case NK::TypedValue: {
            return buildExpr(node->children[1].get());
        }

        case NK::ListLiteral: {
            int count = static_cast<int>(node->children.size());
            IRNode* listNode = graph.makeNewList(count);
            currentBlock->nodes.push_back(listNode);

            for (int i = 0; i < count; ++i) {
                IRNode* elem = buildExpr(node->children[i].get());
                IRNode* idx = graph.createConstant(std::to_string(i));
                currentBlock->nodes.push_back(idx);
                IRNode* setNode = graph.makeListSet(listNode, idx, elem);
                currentBlock->nodes.push_back(setNode);
            }
            return listNode;
        }

        default:
            throw std::runtime_error("Unknown expression node");
        }
    }

    // Resolve a module name to a .tyne file path in the lib search paths
    std::string resolveImport(const std::string& name) {
        // name may be "io", "std.io", etc. — try <name>.tyne and <name/as/path>.tyne
        std::string file = name;
        for (auto& c : file) if (c == '.') c = '/';
        file += ".tyne";

        for (auto& base : libSearchPaths) {
            auto candidate = base / file;
            if (std::filesystem::exists(candidate))
                return candidate.string();
            // Also try just the last component
            auto simple = base / (name.substr(name.rfind('.') + 1) + ".tyne");
            if (std::filesystem::exists(simple))
                return simple.string();
        }
        return "";
    }

    void buildStmt(Parser::ASTNode* node) {
        using NK = Parser::NodeKind;

        switch (node->kind) {

        case NK::VariableDecl: {
            std::string name = node->children[1]->value;

            IRNode* value = nullptr;
            if (node->children.size() == 3)
                value = buildExpr(node->children[2].get());
            else {
                value = graph.createConstant("0");
                currentBlock->nodes.push_back(value);
            }

            env[name] = value;
            lastUse[name] = value;
            return;
        }

        case NK::Assignment: {
            // Call-as-statement: __call__ with one child (the call expr)
            if (node->value == "__call__") {
                buildExpr(node->children[0].get());
                return;
            }

            std::string name = node->children[0]->value;
            IRNode* newValue = buildExpr(node->children[1].get());

            auto it = env.find(name);
            if (it != env.end()) {
                IRNode* del = graph.createNode(IRNodeKind::Delete);
                graph.addInput(del, it->second);
                currentBlock->nodes.push_back(del);
            }

            env[name] = newValue;
            lastUse[name] = newValue;
            return;
        }

        case NK::ReturnStmt: {
            IRNode* retVal = nullptr;
            if (!node->children.empty())
                retVal = buildExpr(node->children[0].get());
            else {
                retVal = graph.createConstant("0");
                currentBlock->nodes.push_back(retVal);
            }
            IRNode* ret = graph.makeReturn(retVal);
            currentBlock->terminator = ret;
            // Switch to a new (dead) block so subsequent stmts don't crash
            currentBlock = graph.createBlock();
            return;
        }

        case NK::IfStmt: {
            auto cond = buildExpr(node->children[0].get());

            auto thenBlock  = graph.createBlock();
            auto elseBlock  = graph.createBlock();
            auto mergeBlock = graph.createBlock();

            IRNode* br = graph.makeBranch(cond, thenBlock, elseBlock);
            currentBlock->terminator = br;

            auto envBefore  = env;
            auto lastBefore = lastUse;

            currentBlock = thenBlock;
            buildStmt(node->children[1].get());
            if (!currentBlock->terminator) {
                IRNode* j1 = graph.makeJump(mergeBlock);
                currentBlock->terminator = j1;
            }
            auto envThen = env;

            env      = envBefore;
            lastUse  = lastBefore;

            currentBlock = elseBlock;
            if (node->children.size() == 3)
                buildStmt(node->children[2].get());
            if (!currentBlock->terminator) {
                IRNode* j2 = graph.makeJump(mergeBlock);
                currentBlock->terminator = j2;
            }
            auto envElse = env;

            currentBlock = mergeBlock;

            for (auto& [name, valThen] : envThen) {
                auto itElse = envElse.find(name);
                if (itElse != envElse.end() && itElse->second != valThen) {
                    IRNode* phi = graph.makePhi({ valThen, itElse->second });
                    currentBlock->nodes.push_back(phi);
                    env[name]     = phi;
                    lastUse[name] = phi;
                }
            }
            return;
        }

        case NK::WhileStmt: {
            auto header = graph.createBlock();
            auto body   = graph.createBlock();
            auto exit   = graph.createBlock();

            IRNode* j = graph.makeJump(header);
            currentBlock->terminator = j;

            currentBlock = header;
            auto cond = buildExpr(node->children[0].get());
            IRNode* br = graph.makeBranch(cond, body, exit);
            currentBlock->terminator = br;

            currentBlock = body;
            buildStmt(node->children[1].get());
            if (!currentBlock->terminator) {
                IRNode* j2 = graph.makeJump(header);
                currentBlock->terminator = j2;
            }

            currentBlock = exit;
            return;
        }

        case NK::ForStmt: {
            buildStmt(node->children[0].get());

            auto header = graph.createBlock();
            auto body   = graph.createBlock();
            auto step   = graph.createBlock();
            auto exit   = graph.createBlock();

            IRNode* j = graph.makeJump(header);
            currentBlock->terminator = j;

            currentBlock = header;
            auto cond = buildExpr(node->children[1].get());
            IRNode* br = graph.makeBranch(cond, body, exit);
            currentBlock->terminator = br;

            currentBlock = body;
            buildStmt(node->children[3].get());
            if (!currentBlock->terminator) {
                IRNode* j2 = graph.makeJump(step);
                currentBlock->terminator = j2;
            }

            currentBlock = step;
            buildExpr(node->children[2].get());
            if (!currentBlock->terminator) {
                IRNode* j3 = graph.makeJump(header);
                currentBlock->terminator = j3;
            }

            currentBlock = exit;
            return;
        }

        case NK::Block: {
            for (auto& child : node->children)
                buildStmt(child.get());
            return;
        }

        case NK::ClassDef: {
            for (auto& child : node->children) {
                if (child->kind == Parser::NodeKind::Constructor)
                    buildStmt(child.get());
            }
            return;
        }

        case NK::Constructor: {
            size_t paramCount = node->children.size() - 1;
            for (size_t i = 0; i < paramCount; ++i) {
                auto& param = node->children[i];
                if (param->kind == Parser::NodeKind::VariableDecl) {
                    std::string paramName = param->children[1]->value;
                    IRNode* paramValue = graph.createConstant("0");
                    currentBlock->nodes.push_back(paramValue);
                    env[paramName]     = paramValue;
                    lastUse[paramName] = paramValue;
                }
            }
            if (paramCount < node->children.size())
                buildStmt(node->children.back().get());
            return;
        }

        case NK::NamespaceDef: {
            for (auto& child : node->children)
                buildStmt(child.get());
            return;
        }

        case NK::StructDef:
            return; // no runtime code

        case NK::EntryPoint: {
            if (!node->children.empty())
                buildStmt(node->children[0].get());
            return;
        }

        case NK::FunctionDecl: {
            if (!node->children.empty())
                buildStmt(node->children.back().get());
            return;
        }

        case NK::ImportStmt: {
            // Try to compile the stdlib module into the same IR graph
            std::string path = resolveImport(node->value);
            if (!path.empty())
                compileAndBuildIR(path, graph);
            // Silently ignore unresolvable imports (external runtime)
            return;
        }

        default:
            throw std::runtime_error("Unknown statement");
        }
    }

public:
    void buildProgram(Parser::ASTNode* root) {
        for (auto& child : root->children)
            buildStmt(child.get());

        for (auto& [name, last] : lastUse) {
            IRNode* del = graph.createNode(IRNodeKind::Delete);
            graph.addInput(del, last);
            currentBlock->nodes.push_back(del);
        }
    }
};
