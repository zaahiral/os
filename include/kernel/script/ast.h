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

    class AssignExpression : public Expression
    {
    public:
        AssignExpression(uint64_t line, char* name, Expression* value) : Expression(line), m_name(name), m_value(value) {}
        ~AssignExpression() { delete[] m_name; delete m_value; }
        Value evaluate(Interpreter& interpreter);
    private:
        char* m_name;
        Expression* m_value;
    };

    class UnaryExpression : public Expression
    {
    public:
        UnaryExpression(uint64_t line, Token::Type operation, Expression* operand) : Expression(line), m_operation(operation), m_operand(operand) {}
        ~UnaryExpression() { delete m_operand; }
        Value evaluate(Interpreter& interpreter);
    private:
        Token::Type m_operation;
        Expression* m_operand;
    };

    class BinaryExpression : public Expression
    {
    public:
        BinaryExpression(uint64_t line, Token::Type operation, Expression* left, Expression* right) : Expression(line), m_operation(operation), m_left(left), m_right(right) {}
        ~BinaryExpression() { delete m_left; delete m_right; }
        Value evaluate(Interpreter& interpreter);
    private:
        Token::Type m_operation;
        Expression* m_left;
        Expression* m_right;
    };

    class CallExpression : public Expression
    {
    public:
        CallExpression(uint64_t line, char* name) : Expression(line), m_name(name) {}
        ~CallExpression();
        Value evaluate(Interpreter& interpreter);
        void add_argument(Expression* argument) { m_arguments.append(argument); }
    private:
        char* m_name;
        Common::DoublyLinkedList<Expression*> m_arguments;
    };

    class Statement
    {
    public:
        // Executing a statement either continues normally, returns from a function, or stops on an error.
        enum Result { NORMAL, RETURN, ERROR };
        Statement(uint64_t line) : m_line(line) {}
        virtual ~Statement() = default;
        virtual Statement::Result execute(Interpreter& interpreter) = 0;
    protected:
        uint64_t m_line;
    };

    class ExpressionStatement : public Statement
    {
    public:
        ExpressionStatement(uint64_t line, Expression* expression) : Statement(line), m_expression(expression) {}
        ~ExpressionStatement() { delete m_expression; }
        Statement::Result execute(Interpreter& interpreter);
    private:
        Expression* m_expression;
    };
