#ifndef PARSER_H
#define PARSER_H

#include "types.h"
#include "script/lexer.h"
#include "script/ast.h"

namespace Script
{
    class Interpreter;

    // A recursive descent parser, with one function for each level of operator precedence.
    // Errors are reported to the interpreter, and parsing stops at the first one.
    class Parser
    {
    public:
        Parser(const char* source, Interpreter& interpreter) : m_lexer(source), m_interpreter(interpreter) {}
        // Parses the whole program, and returns nullptr if there is a syntax error.
        BlockStatement* parse_program();
    private:
        Lexer m_lexer;
        Interpreter& m_interpreter;
        Token m_current;
        Token m_previous;
        bool m_hasError = false;

        void advance();
        bool check(Token::Type type) { return m_current.type == type; }
        bool match(Token::Type type);
        bool consume(Token::Type type, const char* message);
        void error(const char* message, uint64_t line);
        char* copy_identifier(Token token);
        char* copy_string(Token token);
        Expression* make_binary(Token operation, Expression* left, Expression* right);

        Statement* declaration();
        Statement* var_declaration();
        Statement* function_declaration();
        Statement* statement();
        Statement* if_statement();
        Statement* while_statement();
        Statement* return_statement();
        Statement* expression_statement();
        BlockStatement* block();

        Expression* expression();
        Expression* assignment();
        Expression* logical_or();
        Expression* logical_and();
        Expression* equality();
        Expression* comparison();
        Expression* term();
        Expression* factor();
        Expression* unary();
        Expression* primary();
    };
}

#endif
