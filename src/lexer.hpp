#pragma once
#include <unordered_map>
#include <string>
#include <cctype>

class Lexer {
private:
    char ch;
    long pos;
    int line;
    std::string text;

public:
    Lexer(std::string input) {
        text = input;
        pos = 0;
        line = 1;
        ch = text[pos];
    }

    enum class TokenType {
        INT32, 
        INT64, 
        INT128, 
        UINT32,
        UINT64,
        UINT128,
        FLOAT,
        DOUBLE,
        LIST,
        IF,
        ELSE,
        WHILE,
        FOR,
        RETURN,
        IDENTIFIER,
        STRING,
        RAW_STRING,
        NUMBER,
        OPERATOR,
        CLASS_DEF,
        FUNCTION_DEF,
        STRUCT_DEF,
        NAMESPACE_DEF,
        ENTRY_POINT,
        IMPORT,
        TOKEN_ERROR,
        EOF_TOKEN
    };

    struct Token {
        TokenType type;
        std::string value;
        int line;
    };

    std::unordered_map<std::string, TokenType> keywords = {
        {"int32", TokenType::INT32},
        {"int64", TokenType::INT64},
        {"int128", TokenType::INT128},
        {"uint32", TokenType::UINT32},
        {"uint64", TokenType::UINT64},
        {"uint128", TokenType::UINT128},
        {"float", TokenType::FLOAT},
        {"double", TokenType::DOUBLE},
        {"list", TokenType::LIST},
        {"if", TokenType::IF},
        {"else", TokenType::ELSE},
        {"while", TokenType::WHILE},
        {"for", TokenType::FOR},
        {"return", TokenType::RETURN},
        {"class", TokenType::CLASS_DEF},
        {"function", TokenType::FUNCTION_DEF},
        {"struct", TokenType::STRUCT_DEF},
        {"namespace", TokenType::NAMESPACE_DEF},
        {"entry", TokenType::ENTRY_POINT},
        {"import", TokenType::IMPORT}
    };

    std::unordered_map<std::string, TokenType> operators = {
        {"+", TokenType::OPERATOR},
        {"-", TokenType::OPERATOR},
        {"*", TokenType::OPERATOR},
        {"/", TokenType::OPERATOR},
        {"=", TokenType::OPERATOR},
        {"==", TokenType::OPERATOR},
        {"!=", TokenType::OPERATOR},
        {"<", TokenType::OPERATOR},
        {">", TokenType::OPERATOR},
        {"<=", TokenType::OPERATOR},
        {">=", TokenType::OPERATOR},
        {"&&", TokenType::OPERATOR},
        {"|", TokenType::OPERATOR},
        {"||", TokenType::OPERATOR},
        {"!", TokenType::OPERATOR},
        {"&", TokenType::OPERATOR},

        {"^", TokenType::OPERATOR},
        {"~", TokenType::OPERATOR},
        {"+=", TokenType::OPERATOR},
        {"-=", TokenType::OPERATOR},
        {"*=", TokenType::OPERATOR},
        {"/=", TokenType::OPERATOR},
        {"%=", TokenType::OPERATOR},
        {"++", TokenType::OPERATOR},
        {"--", TokenType::OPERATOR},
        {"(", TokenType::OPERATOR},
        {")", TokenType::OPERATOR},
        {"{", TokenType::OPERATOR},
        {"}", TokenType::OPERATOR},
        {"[", TokenType::OPERATOR},
        {"]", TokenType::OPERATOR},
        {";", TokenType::OPERATOR},
        {",", TokenType::OPERATOR},
        {".", TokenType::OPERATOR}
    };

    char peek() {
        return (pos + 1 < text.size()) ? text[pos + 1] : '\0';
    }

    char advance() {
        if (ch == '\n') {
            line++;
        }
        pos++;
        ch = (pos < text.size()) ? text[pos] : '\0';
        return ch;
    }


    void skipWhitespace() {
        while (isspace(ch)) {
            advance();
        }
    }

    std::string readIdentifier() {
        std::string result;
        while (isalnum(ch) || ch == '_') {
            result += ch;
            advance();
        }
        return result;
    }

    std::string readNumber() {
        std::string result;
        while (isdigit(ch)) {
            result += ch;
            advance();
        }
        return result;
    }

    std::string readString() {
        std::string result;
        advance(); // skip opening quote
        while (ch != '"' && ch != '\0') {
            if (ch == '\\') {
                advance(); // skip backslash
                switch (ch) {
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case 'r': result += '\r'; break;
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '0': result += '\0'; break;
                    default: result += ch; break; // unknown escape, keep as-is
                }
            } else {
                result += ch;
            }
            advance();
        }
        if (ch == '"') {
            advance(); // skip closing quote
        }
        return result;
    }

    std::string readRawString() {
        std::string result;
        advance(); // skip opening quote
        while (ch != '\'' && ch != '\0') {
            result += ch;
            advance();
        }
        if (ch == '\'') {
            advance(); // skip closing quote
        }
        return result;
    }

    void skipComment() {
        if (ch == '#' && peek() == '#') {
            advance(); // skip first '#'
            advance(); // skip second '#'
            while (ch != '\0') {
                if (ch == '#' && peek() == '#') {
                    advance(); // skip first '#'
                    advance(); // skip second '#'
                    break;
                }
                advance();
            }
        } else if (ch == '#') {
            advance(); // skip '#'
            while (ch != '\n' && ch != '\0') {
                advance();
            }
        }
    }

    Token nextToken() {
        skipWhitespace();
        if (ch == '\0') {
            return {TokenType::EOF_TOKEN, "", line};
        }
        if (isalpha(ch) || ch == '_') {
            std::string identifier = readIdentifier();
            if (keywords.count(identifier)) {
                return {keywords[identifier], identifier, line};
            } else {
                return {TokenType::IDENTIFIER, identifier, line};
            }
        }
        if (isdigit(ch)) {
            return {TokenType::NUMBER, readNumber(), line};
        }
        if (ch == '"') {
            return {TokenType::STRING, readString(), line};
        }
        if (ch == '`') {
            return {TokenType::RAW_STRING, readRawString(), line};
        }
        if (ch == '#') {
            skipComment();
            return nextToken();
        }
        // Handle operators - check longest matches first
        std::string op;
        if (pos + 1 < text.size()) {
            op = std::string(1, ch) + text[pos + 1];
            if (operators.count(op)) {
                advance();
                advance();
                return {operators[op], op, line};
            }
        }
        op = std::string(1, ch);
        if (operators.count(op)) {
            advance();
            return {operators[op], op, line};
        }
        char bad = ch;
        advance();
        return {TokenType::TOKEN_ERROR, std::string(1, bad), line};
    }
};
