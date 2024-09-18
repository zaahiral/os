#ifndef LEXER_H
#define LEXER_H

#include "types.h"

namespace Script
{
    struct Token
    {
        enum Type
        {
            NUMBER, STRING, IDENTIFIER,
            VAR, FUNC, IF, ELSE, WHILE, RETURN,
            LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE, COMMA, SEMICOLON,
            PLUS, MINUS, STAR, SLASH, PERCENT,
            EQUAL, EQUAL_EQUAL, BANG, BANG_EQUAL,
            LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,
            AND, OR,
            END, ERROR
        };
        Type type;
        // The text points into the source, so it is not null terminated. For an error token it is the error message.
        const char* start;
        uint64_t length;
        uint64_t line;
    };

    // Splits cScript source code into tokens, one at a time.
    class Lexer
    {
    public:
        Lexer(const char* source) : m_current(source) {}
        Token next_token();
    private:
        const char* m_current;
        uint64_t m_line = 1;
        Token make_token(Token::Type type, const char* start);
        Token make_error_token(const char* message);
        bool match_next(char next);
        void skip_whitespace_and_comments();
        Token::Type identifier_type(const char* start, uint64_t length);
    };
}

#endif
