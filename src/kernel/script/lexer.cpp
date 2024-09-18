#include "script/lexer.h"
#include "libc/string.h"

namespace Script
{
    namespace
    {
        bool is_digit(char c) { return c >= '0' && c <= '9'; }
        bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

        // Checks whether text of the given length is exactly the keyword.
        bool is_keyword(const char* keyword, const char* text, uint64_t length)
        {
            for (uint64_t i = 0; i < length; i++)
            {
                if (keyword[i] != text[i]) { return false; }
            }
            return !keyword[length];
        }

        struct Keyword
        {
            const char* text;
            Token::Type type;
        };

        constexpr Keyword KEYWORDS[] = {
            {"var", Token::VAR}, {"func", Token::FUNC}, {"if", Token::IF},
            {"else", Token::ELSE}, {"while", Token::WHILE}, {"return", Token::RETURN},
        };
    }

    Token Lexer::make_token(Token::Type type, const char* start)
    {
        return Token{type, start, (uint64_t)(m_current - start), m_line};
    }

    Token Lexer::make_error_token(const char* message)
    {
        return Token{Token::ERROR, message, strlen(message), m_line};
    }

    void Lexer::skip_whitespace_and_comments()
    {
        while (1)
        {
            if (*m_current == '\n') { m_line++; m_current++; }
            else if (*m_current == ' ' || *m_current == '\t' || *m_current == '\r') { m_current++; }
            // Comments start with // and run to the end of the line.
            else if (m_current[0] == '/' && m_current[1] == '/')
            {
                while (*m_current && *m_current != '\n') { m_current++; }
            }
            else { return; }
        }
    }

    Token::Type Lexer::identifier_type(const char* start, uint64_t length)
    {
        for (uint64_t i = 0; i < sizeof(KEYWORDS) / sizeof(Keyword); i++)
        {
            if (is_keyword(KEYWORDS[i].text, start, length)) { return KEYWORDS[i].type; }
        }
        return Token::IDENTIFIER;
    }

    // Some operators are two characters long, such as == and <=, so we check the next character.
    bool Lexer::match_next(char next)
    {
        if (*m_current != next) { return false; }
        m_current++;
        return true;
    }

    Token Lexer::next_token()
    {
        skip_whitespace_and_comments();
        const char* start = m_current;
        if (!*m_current) { return make_token(Token::END, start); }

        char c = *m_current++;
        if (is_digit(c))
        {
            while (is_digit(*m_current)) { m_current++; }
            return make_token(Token::NUMBER, start);
        }
        if (is_letter(c))
        {
            while (is_letter(*m_current) || is_digit(*m_current)) { m_current++; }
            return make_token(identifier_type(start, m_current - start), start);
        }
        if (c == '"')
        {
            // The string token includes its quotes, and the parser removes them and handles escapes.
            while (*m_current && *m_current != '"' && *m_current != '\n')
            {
                if (*m_current == '\\' && m_current[1]) { m_current++; }
                m_current++;
            }
            if (*m_current != '"') { return make_error_token("unterminated string."); }
            m_current++;
            return make_token(Token::STRING, start);
        }

        switch (c)
        {
        case '(': return make_token(Token::LEFT_PAREN, start);
        case ')': return make_token(Token::RIGHT_PAREN, start);
        case '{': return make_token(Token::LEFT_BRACE, start);
        case '}': return make_token(Token::RIGHT_BRACE, start);
        case ',': return make_token(Token::COMMA, start);
        case ';': return make_token(Token::SEMICOLON, start);
        case '+': return make_token(Token::PLUS, start);
        case '-': return make_token(Token::MINUS, start);
        case '*': return make_token(Token::STAR, start);
        case '/': return make_token(Token::SLASH, start);
        case '%': return make_token(Token::PERCENT, start);
        case '=': return make_token(match_next('=') ? Token::EQUAL_EQUAL : Token::EQUAL, start);
        case '!': return make_token(match_next('=') ? Token::BANG_EQUAL : Token::BANG, start);
        case '<': return make_token(match_next('=') ? Token::LESS_EQUAL : Token::LESS, start);
        case '>': return make_token(match_next('=') ? Token::GREATER_EQUAL : Token::GREATER, start);
        case '&': if (match_next('&')) { return make_token(Token::AND, start); } break;
        case '|': if (match_next('|')) { return make_token(Token::OR, start); } break;
        default: break;
        }
        return make_error_token("unexpected character.");
    }
}
