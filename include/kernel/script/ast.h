#ifndef AST_H
#define AST_H

#include "types.h"
#include "script/lexer.h"
#include "common/doublyLinkedList.h"

namespace Script
{
    class Interpreter;

    // Every cScript value is either nothing, a 64-bit integer, or a string.
    struct Value
    {
        enum Type { NONE, INTEGER, STRING };
        Type type = NONE;
        int64_t integer = 0;
        const char* string = nullptr;

        static Value from_integer(int64_t integer) { Value value; value.type = Value::INTEGER; value.integer = integer; return value; }
        static Value from_string(const char* string) { Value value; value.type = Value::STRING; value.string = string; return value; }
        bool is_true();
    };

    class Expression
    {
    public:
        Expression(uint64_t line) : m_line(line) {}
        virtual ~Expression() = default;
        virtual Value evaluate(Interpreter& interpreter) = 0;
        // Only a variable can be assigned to, so other expressions return nullptr.
        virtual char* get_variable_name() { return nullptr; }
    protected:
        uint64_t m_line;
    };

    class NumberExpression : public Expression
    {
    public:
        NumberExpression(uint64_t line, int64_t number) : Expression(line), m_number(number) {}
        Value evaluate(Interpreter& interpreter);
    private:
        int64_t m_number;
    };

    class StringExpression : public Expression
    {
    public:
        StringExpression(uint64_t line, char* text) : Expression(line), m_text(text) {}
        ~StringExpression() { delete[] m_text; }
        Value evaluate(Interpreter& interpreter);
    private:
        char* m_text;
    };

    class VariableExpression : public Expression
    {
    public:
        VariableExpression(uint64_t line, char* name) : Expression(line), m_name(name) {}
        ~VariableExpression() { delete[] m_name; }
        Value evaluate(Interpreter& interpreter);
        char* get_variable_name() { return m_name; }
    private:
        char* m_name;
    };
