#pragma once
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include "lexer.hpp"

class Parser {
private:
    std::vector<Lexer::Token> tokens;
    int pos;

    // --- Token navigation ---
    inline Lexer::Token& current() {
        return tokens[pos];
    }

    inline Lexer::Token& advance() {
        if (pos < static_cast<int>(tokens.size()) - 1)
            pos++;
        return tokens[pos];
    }

    inline Lexer::Token& peek(int offset = 1) {
        int index = pos + offset;
        if (index >= 0 && index < static_cast<int>(tokens.size()))
            return tokens[index];
        return tokens.back(); // EOF
    }

    inline bool match(Lexer::TokenType type) {
        if (current().type == type) {
            advance();
            return true;
        }
        return false;
    }

    inline bool matchOperator(const std::string& op) {
        if (current().type == Lexer::TokenType::OPERATOR &&
            current().value == op) {
            advance();
            return true;
        }
        return false;
    }

    // --- AST ---
public:
    enum class NodeKind {
        Program,
        VariableDecl,
        Assignment,
        Type,
        MemberAccess,
        Identifier,
        NumberLiteral,
        StringLiteral,
        RawStringLiteral,
        ListLiteral,
        TypedValue,
        BinaryExpr,
        CallExpr,
        IfStmt,
        WhileStmt,
        ForStmt,
        Block,
        ClassDef,
        Constructor,
        NamespaceDef,
        StructDef,
        EntryPoint,
        ImportStmt,
        FunctionDecl,
        ReturnStmt,
    };

    struct ASTNode {
        NodeKind kind;
        int line;
        std::string value;
        std::vector<std::unique_ptr<ASTNode>> children;

        ASTNode(NodeKind k, int l) : kind(k), line(l) {}
    };

private:
    // --- Helpers ---
    bool isTypeToken(Lexer::TokenType t) {
        using TT = Lexer::TokenType;
        return t == TT::INT32 || t == TT::INT64 || t == TT::INT128 ||
               t == TT::UINT32 || t == TT::UINT64 || t == TT::UINT128 ||
               t == TT::FLOAT || t == TT::DOUBLE || t == TT::LIST;
    }

    // --- Parsing primitives ---
    std::unique_ptr<ASTNode> parseIdentifier() {
        if (current().type != Lexer::TokenType::IDENTIFIER)
            throw std::runtime_error("Expected identifier");
        auto node = std::make_unique<ASTNode>(NodeKind::Identifier, current().line);
        node->value = current().value;
        advance();
        return node;
    }

    std::unique_ptr<ASTNode> parseType() {
        if (!isTypeToken(current().type))
            throw std::runtime_error("Expected type");
        auto node = std::make_unique<ASTNode>(NodeKind::Type, current().line);
        node->value = current().value;
        advance();
        return node;
    }

    std::unique_ptr<ASTNode> parseNumberLiteral() {
        auto node = std::make_unique<ASTNode>(NodeKind::NumberLiteral, current().line);
        node->value = current().value;
        advance();
        return node;
    }

    std::unique_ptr<ASTNode> parseStringLiteral() {
        auto node = std::make_unique<ASTNode>(NodeKind::StringLiteral, current().line);
        node->value = current().value;
        advance();
        return node;
    }

    std::unique_ptr<ASTNode> parseRawStringLiteral() {
        auto node = std::make_unique<ASTNode>(NodeKind::RawStringLiteral, current().line);
        node->value = current().value;
        advance();
        return node;
    }

    std::unique_ptr<ASTNode> parseMemberAccess() {
        auto node = std::make_unique<ASTNode>(NodeKind::MemberAccess, current().line);
        node->children.push_back(parseIdentifier());
        while (matchOperator(".")) {
            node->children.push_back(parseIdentifier());
        }
        return node;
    }

    std::unique_ptr<ASTNode> parseListLiteral() {
        auto node = std::make_unique<ASTNode>(NodeKind::ListLiteral, current().line);
        // '[' wurde bereits gematcht
        while (!matchOperator("]")) {
            node->children.push_back(parseExpression());
            matchOperator(",");
        }
        return node;
    }

    std::unique_ptr<ASTNode> parseTypedValue() {
        auto node = std::make_unique<ASTNode>(NodeKind::TypedValue, current().line);
        node->children.push_back(parseType());
        node->children.push_back(parseExpression());
        return node;
    }

    // --- Expressions ---

    // Primary: literal / identifier / call / paren
    std::unique_ptr<ASTNode> parsePrimary() {
        using TT = Lexer::TokenType;

        if (current().type == TT::NUMBER)
            return parseNumberLiteral();

        if (current().type == TT::STRING)
            return parseStringLiteral();

        if (current().type == TT::RAW_STRING)
            return parseRawStringLiteral();

        if (matchOperator("["))
            return parseListLiteral();

        if (isTypeToken(current().type))
            return parseTypedValue();

        // Parenthesised expression
        if (current().type == TT::OPERATOR && current().value == "(") {
            advance();
            auto inner = parseExpression();
            if (!matchOperator(")"))
                throw std::runtime_error("Expected ')' after expression at line " +
                                         std::to_string(current().line));
            return inner;
        }

        if (current().type == TT::IDENTIFIER) {
            // Collect dotted name: a.b.c
            int ln = current().line;
            std::string name = current().value;
            advance();
            while (current().type == TT::OPERATOR && current().value == ".") {
                advance();
                if (current().type != TT::IDENTIFIER)
                    throw std::runtime_error("Expected identifier after '.' at line " +
                                             std::to_string(current().line));
                name += "." + current().value;
                advance();
            }

            // Call expression: name(args...)
            if (current().type == TT::OPERATOR && current().value == "(") {
                advance();
                auto call = std::make_unique<ASTNode>(NodeKind::CallExpr, ln);
                call->value = name;
                while (!(current().type == TT::OPERATOR && current().value == ")")) {
                    call->children.push_back(parseExpression());
                    if (current().type == TT::OPERATOR && current().value == ",")
                        advance();
                }
                advance(); // consume ')'
                return call;
            }

            // Plain identifier / member access
            auto node = std::make_unique<ASTNode>(NodeKind::MemberAccess, ln);
            // Split by '.' and add children
            std::string part;
            for (char c : name) {
                if (c == '.') {
                    auto id = std::make_unique<ASTNode>(NodeKind::Identifier, ln);
                    id->value = part;
                    node->children.push_back(std::move(id));
                    part.clear();
                } else {
                    part += c;
                }
            }
            auto id = std::make_unique<ASTNode>(NodeKind::Identifier, ln);
            id->value = part;
            node->children.push_back(std::move(id));
            if (node->children.size() == 1) {
                // Simplify to plain Identifier
                auto simple = std::make_unique<ASTNode>(NodeKind::Identifier, ln);
                simple->value = part;
                return simple;
            }
            return node;
        }

        throw std::runtime_error("Unexpected token in expression at line " +
                                 std::to_string(current().line));
    }

    static int operatorPrecedence(const std::string& op) {
        if (op == "||") return 1;
        if (op == "&&") return 2;
        if (op == "|")  return 3;
        if (op == "^")  return 4;
        if (op == "&")  return 5;
        if (op == "==" || op == "!=") return 6;
        if (op == "<" || op == ">" || op == "<=" || op == ">=") return 7;
        if (op == "+" || op == "-") return 8;
        if (op == "*" || op == "/" || op == "%") return 9;
        return -1;
    }

    std::unique_ptr<ASTNode> parseBinaryRHS(int minPrec, std::unique_ptr<ASTNode> lhs) {
        while (true) {
            if (current().type != Lexer::TokenType::OPERATOR) break;
            int prec = operatorPrecedence(current().value);
            if (prec < minPrec) break;

            std::string op = current().value;
            int ln = current().line;
            advance();

            auto rhs = parsePrimary();

            // Right-associativity check (none here, all left-assoc)
            int nextPrec = (current().type == Lexer::TokenType::OPERATOR)
                           ? operatorPrecedence(current().value) : -1;
            if (nextPrec > prec)
                rhs = parseBinaryRHS(prec + 1, std::move(rhs));

            auto bin = std::make_unique<ASTNode>(NodeKind::BinaryExpr, ln);
            bin->value = op;
            bin->children.push_back(std::move(lhs));
            bin->children.push_back(std::move(rhs));
            lhs = std::move(bin);
        }
        return lhs;
    }

    std::unique_ptr<ASTNode> parseExpression() {
        return parseBinaryRHS(0, parsePrimary());
    }

    // --- Blocks ---
    std::unique_ptr<ASTNode> parseBlock() {
        int line = current().line;
        if (!matchOperator("{"))
            throw std::runtime_error("Expected '{' for block");
        auto node = std::make_unique<ASTNode>(NodeKind::Block, line);
        while (!matchOperator("}")) {
            node->children.push_back(parseStatement());
        }
        return node;
    }

    // --- Statements: var decl & assignment ---
    std::unique_ptr<ASTNode> parseVariableDecl() {
        auto typeNode = parseType();

        if (current().type != Lexer::TokenType::IDENTIFIER)
            throw std::runtime_error("Expected identifier after type");

        auto idNode = parseIdentifier();

        auto node = std::make_unique<ASTNode>(NodeKind::VariableDecl, idNode->line);
        node->children.push_back(std::move(typeNode));
        node->children.push_back(std::move(idNode));

        if (matchOperator("=")) {
            node->children.push_back(parseExpression());
        }

        if (!matchOperator(";"))
            throw std::runtime_error("Expected ';' after variable declaration");

        return node;
    }

    std::unique_ptr<ASTNode> parseAssignment() {
        auto idNode = parseIdentifier();

        if (!matchOperator("="))
            throw std::runtime_error("Expected '=' in assignment");

        auto exprNode = parseExpression();

        if (!matchOperator(";"))
            throw std::runtime_error("Expected ';' after assignment");

        auto node = std::make_unique<ASTNode>(NodeKind::Assignment, idNode->line);
        node->children.push_back(std::move(idNode));
        node->children.push_back(std::move(exprNode));

        return node;
    }

    // --- if / while / for ---
    std::unique_ptr<ASTNode> parseIf() {
        int line = current().line;
        advance(); // skip 'if'

        if (!matchOperator("("))
            throw std::runtime_error("Expected '(' after if");

        auto cond = parseExpression();

        if (!matchOperator(")"))
            throw std::runtime_error("Expected ')' after if condition");

        auto thenBlock = parseBlock();

        std::unique_ptr<ASTNode> elseBlock = nullptr;
        if (current().type == Lexer::TokenType::ELSE) {
            advance();
            elseBlock = parseBlock();
        }

        auto node = std::make_unique<ASTNode>(NodeKind::IfStmt, line);
        node->children.push_back(std::move(cond));
        node->children.push_back(std::move(thenBlock));
        if (elseBlock)
            node->children.push_back(std::move(elseBlock));

        return node;
    }

    std::unique_ptr<ASTNode> parseWhile() {
        int line = current().line;
        advance(); // skip 'while'

        if (!matchOperator("("))
            throw std::runtime_error("Expected '(' after while");

        auto cond = parseExpression();

        if (!matchOperator(")"))
            throw std::runtime_error("Expected ')' after while condition");

        auto body = parseBlock();

        auto node = std::make_unique<ASTNode>(NodeKind::WhileStmt, line);
        node->children.push_back(std::move(cond));
        node->children.push_back(std::move(body));

        return node;
    }

    std::unique_ptr<ASTNode> parseFor() {
        int line = current().line;
        advance(); // skip 'for'

        if (!matchOperator("("))
            throw std::runtime_error("Expected '(' after for");

        auto init = parseStatement();
        auto cond = parseExpression();

        if (!matchOperator(";"))
            throw std::runtime_error("Expected ';' in for header");

        auto step = parseExpression();

        if (!matchOperator(")"))
            throw std::runtime_error("Expected ')' after for header");

        auto body = parseBlock();

        auto node = std::make_unique<ASTNode>(NodeKind::ForStmt, line);
        node->children.push_back(std::move(init));
        node->children.push_back(std::move(cond));
        node->children.push_back(std::move(step));
        node->children.push_back(std::move(body));

        return node;
    }

    // --- class / constructor / struct ---
    std::unique_ptr<ASTNode> parseConstructor(const std::string& className) {
        int line = current().line;
        advance(); // skip 'function'

        if (current().type != Lexer::TokenType::IDENTIFIER ||
            current().value != className)
            throw std::runtime_error("Constructor name must match class name");

        advance(); // skip constructor name

        if (!matchOperator("("))
            throw std::runtime_error("Expected '(' in constructor");

        auto node = std::make_unique<ASTNode>(NodeKind::Constructor, line);
        node->value = className;

        while (!matchOperator(")")) {
            auto type = parseType();
            auto id = parseIdentifier();

            auto param = std::make_unique<ASTNode>(NodeKind::VariableDecl, id->line);
            param->children.push_back(std::move(type));
            param->children.push_back(std::move(id));

            node->children.push_back(std::move(param));

            matchOperator(",");
        }

        node->children.push_back(parseBlock());

        return node;
    }

    std::unique_ptr<ASTNode> parseClass() {
        int line = current().line;
        advance(); // skip 'class'

        if (current().type != Lexer::TokenType::IDENTIFIER)
            throw std::runtime_error("Expected class name");

        auto name = parseIdentifier();

        // Check for inline constructor: class foo (args) { ... }
        std::unique_ptr<ASTNode> inlineConstructor = nullptr;
        if (matchOperator("(")) {
            // Parse constructor parameters
            inlineConstructor = std::make_unique<ASTNode>(NodeKind::Constructor, line);
            inlineConstructor->value = name->value;

            while (!matchOperator(")")) {
                auto type = parseType();
                auto id = parseIdentifier();

                auto param = std::make_unique<ASTNode>(NodeKind::VariableDecl, id->line);
                param->children.push_back(std::move(type));
                param->children.push_back(std::move(id));

                inlineConstructor->children.push_back(std::move(param));

                matchOperator(",");
            }
        }

        if (!matchOperator("{"))
            throw std::runtime_error("Expected '{' after class name");

        auto node = std::make_unique<ASTNode>(NodeKind::ClassDef, line);
        node->value = name->value;

        // Add inline constructor if present
        if (inlineConstructor) {
            // Parse constructor body
            inlineConstructor->children.push_back(parseBlock());
            node->children.push_back(std::move(inlineConstructor));
        }

        while (!matchOperator("}")) {
            // constructor: function ClassName(...)
            if (current().type == Lexer::TokenType::FUNCTION_DEF &&
                peek().type == Lexer::TokenType::IDENTIFIER &&
                peek().value == name->value) {
                node->children.push_back(parseConstructor(name->value));
                continue;
            }

            if (isTypeToken(current().type)) {
                node->children.push_back(parseVariableDecl());
                continue;
            }

            throw std::runtime_error("Unexpected token in class body at line " +
                                     std::to_string(current().line));
        }

        return node;
    }

    std::unique_ptr<ASTNode> parseStruct() {
        int line = current().line;
        advance(); // skip 'struct'

        if (current().type != Lexer::TokenType::IDENTIFIER)
            throw std::runtime_error("Expected struct name");

        auto name = parseIdentifier();

        if (!matchOperator("{"))
            throw std::runtime_error("Expected '{' after struct name");

        auto node = std::make_unique<ASTNode>(NodeKind::StructDef, line);
        node->value = name->value;

        while (!matchOperator("}")) {
            if (isTypeToken(current().type)) {
                node->children.push_back(parseVariableDecl());
                continue;
            }

            throw std::runtime_error("Unexpected token in struct body at line " +
                                     std::to_string(current().line));
        }

        return node;
    }

    // --- import / namespace / entry ---
    std::unique_ptr<ASTNode> parseImport() {
        int line = current().line;
        advance(); // skip 'import'

        auto node = std::make_unique<ASTNode>(NodeKind::ImportStmt, line);

        // Accept both:  import io;   and   import "io";
        if (current().type == Lexer::TokenType::STRING) {
            node->value = current().value; // lexer already stripped quotes
            advance();
        } else {
            if (current().type != Lexer::TokenType::IDENTIFIER)
                throw std::runtime_error("Expected identifier after import");

            std::string fullPath = current().value;
            advance();

            while (matchOperator(".")) {
                if (current().type != Lexer::TokenType::IDENTIFIER)
                    throw std::runtime_error("Expected identifier after '.' in import");
                fullPath += "." + current().value;
                advance();
            }
            node->value = fullPath;
        }

        if (!matchOperator(";"))
            throw std::runtime_error("Expected ';' after import");

        return node;
    }

    std::unique_ptr<ASTNode> parseNamespace() {
        int line = current().line;
        advance(); // skip 'namespace'

        if (current().type != Lexer::TokenType::IDENTIFIER)
            throw std::runtime_error("Expected namespace name");

        std::string fullName = current().value;
        advance();

        while (matchOperator(".")) {
            if (current().type != Lexer::TokenType::IDENTIFIER)
                throw std::runtime_error("Expected identifier after '.' in namespace");
            fullName += "." + current().value;
            advance();
        }

        if (!matchOperator("{"))
            throw std::runtime_error("Expected '{' after namespace name");

        auto node = std::make_unique<ASTNode>(NodeKind::NamespaceDef, line);
        node->value = fullName;

        while (!matchOperator("}")) {
            if (current().type == Lexer::TokenType::NAMESPACE_DEF) {
                node->children.push_back(parseNamespace());
                continue;
            }
            if (current().type == Lexer::TokenType::CLASS_DEF) {
                node->children.push_back(parseClass());
                continue;
            }
            if (current().type == Lexer::TokenType::STRUCT_DEF) {
                node->children.push_back(parseStruct());
                continue;
            }
            if (current().type == Lexer::TokenType::IMPORT) {
                node->children.push_back(parseImport());
                continue;
            }

            node->children.push_back(parseStatement());
        }

        return node;
    }

    std::unique_ptr<ASTNode> parseFunction() {
        int line = current().line;

        // Parse return type
        auto returnType = parseType();

        // Parse function name
        if (current().type != Lexer::TokenType::IDENTIFIER)
            throw std::runtime_error("Expected function name");
        auto funcName = parseIdentifier();

        // Parse parameters
        if (!matchOperator("("))
            throw std::runtime_error("Expected '(' after function name");

        auto node = std::make_unique<ASTNode>(NodeKind::FunctionDecl, line);
        node->value = funcName->value;

        // Add return type as first child
        node->children.push_back(std::move(returnType));

        // Parse parameters
        while (!matchOperator(")")) {
            auto paramType = parseType();
            auto paramName = parseIdentifier();

            auto param = std::make_unique<ASTNode>(NodeKind::VariableDecl, paramName->line);
            param->children.push_back(std::move(paramType));
            param->children.push_back(std::move(paramName));

            node->children.push_back(std::move(param));

            matchOperator(",");
        }

        // Parse function body
        node->children.push_back(parseBlock());

        return node;
    }

    std::unique_ptr<ASTNode> parseEntry() {
        int line = current().line;
        advance(); // skip 'entry'

        auto node = std::make_unique<ASTNode>(NodeKind::EntryPoint, line);
        node->children.push_back(parseBlock());

        return node;
    }

    // --- return statement ---
    std::unique_ptr<ASTNode> parseReturn() {
        int line = current().line;
        advance(); // skip 'return'
        auto node = std::make_unique<ASTNode>(NodeKind::ReturnStmt, line);
        // optional expression before ';'
        if (!(current().type == Lexer::TokenType::OPERATOR && current().value == ";"))
            node->children.push_back(parseExpression());
        if (!matchOperator(";"))
            throw std::runtime_error("Expected ';' after return at line " +
                                     std::to_string(current().line));
        return node;
    }

    // --- Statement dispatcher ---
    std::unique_ptr<ASTNode> parseStatement() {
        using TT = Lexer::TokenType;

        if (current().type == TT::IF)
            return parseIf();

        if (current().type == TT::WHILE)
            return parseWhile();

        if (current().type == TT::FOR)
            return parseFor();

        if (current().type == TT::RETURN)
            return parseReturn();

        if (isTypeToken(current().type))
            return parseVariableDecl();

        if (current().type == TT::IDENTIFIER) {
            // Peek ahead: if name( -> call statement, else assignment
            int saved = pos;
            std::string name = current().value;
            advance();
            // Collect dotted name
            while (current().type == TT::OPERATOR && current().value == ".") {
                advance();
                if (current().type == TT::IDENTIFIER) { name += "." + current().value; advance(); }
            }
            bool isCall = (current().type == TT::OPERATOR && current().value == "(");
            pos = saved; // rewind

            if (isCall) {
                // Parse as call expression statement
                int ln = current().line;
                auto expr = parseExpression(); // will produce CallExpr
                if (!matchOperator(";"))
                    throw std::runtime_error("Expected ';' after call at line " +
                                             std::to_string(current().line));
                // Wrap in a pseudo-assignment with no lhs to carry through IR
                auto node = std::make_unique<ASTNode>(NodeKind::Assignment, ln);
                node->value = "__call__";
                node->children.push_back(std::move(expr));
                return node;
            }
            return parseAssignment();
        }

        throw std::runtime_error("Unknown statement at line " +
                                 std::to_string(current().line));
    }

public:
    Parser(std::vector<Lexer::Token> tokens)
        : tokens(std::move(tokens)), pos(0) {}

    ASTNode* parseProgram() {
        auto root = new ASTNode(NodeKind::Program, 0);

        while (current().type != Lexer::TokenType::EOF_TOKEN) {
            using TT = Lexer::TokenType;

            if (current().type == TT::IMPORT) {
                root->children.push_back(parseImport());
                continue;
            }

            if (current().type == TT::NAMESPACE_DEF) {
                root->children.push_back(parseNamespace());
                continue;
            }

            if (current().type == TT::CLASS_DEF) {
                root->children.push_back(parseClass());
                continue;
            }

            if (current().type == TT::STRUCT_DEF) {
                root->children.push_back(parseStruct());
                continue;
            }

            if (current().type == TT::ENTRY_POINT) {
                root->children.push_back(parseEntry());
                continue;
            }

            // Check for function declaration: type name(...)
            if (isTypeToken(current().type) &&
                peek().type == TT::IDENTIFIER &&
                peek(2).type == TT::OPERATOR &&
                peek(2).value == "(") {
                root->children.push_back(parseFunction());
                continue;
            }

            root->children.push_back(parseStatement());
        }

        return root;
    }
};
