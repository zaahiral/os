#include "script/ast.h"
#include "script/interpreter.h"
#include "libc/string.h"

namespace Script
{
    bool Value::is_true()
    {
        if (type == Value::INTEGER) { return integer != 0; }
        if (type == Value::STRING) { return string[0] != '\0'; }
        return false;
    }

    Value NumberExpression::evaluate(Interpreter& interpreter) { return Value::from_integer(m_number); }

    Value StringExpression::evaluate(Interpreter& interpreter) { return Value::from_string(m_text); }

    Value VariableExpression::evaluate(Interpreter& interpreter) { return interpreter.get_variable(m_name, m_line); }

    Value AssignExpression::evaluate(Interpreter& interpreter)
    {
        auto value = m_value->evaluate(interpreter);
        if (interpreter.has_error()) { return Value(); }
        interpreter.assign_variable(m_name, value, m_line);
        return value;
    }

    Value UnaryExpression::evaluate(Interpreter& interpreter)
    {
        auto operand = m_operand->evaluate(interpreter);
        if (interpreter.has_error()) { return Value(); }
        if (m_operation == Token::BANG) { return Value::from_integer(!operand.is_true()); }

        if (operand.type != Value::INTEGER)
        {
            interpreter.error(m_line, "the operand of - must be an integer.");
            return Value();
        }
        return Value::from_integer(-operand.integer);
    }

    Value BinaryExpression::evaluate(Interpreter& interpreter)
    {
        auto left = m_left->evaluate(interpreter);
        if (interpreter.has_error()) { return Value(); }

        // The right operand of && and || is only evaluated if it can change the result.
        if (m_operation == Token::AND && !left.is_true()) { return Value::from_integer(0); }
        if (m_operation == Token::OR && left.is_true()) { return Value::from_integer(1); }

        auto right = m_right->evaluate(interpreter);
        if (interpreter.has_error()) { return Value(); }

        switch (m_operation)
        {
        case Token::AND:
        case Token::OR:
            return Value::from_integer(right.is_true());
        case Token::EQUAL_EQUAL:
        case Token::BANG_EQUAL:
        {
            bool equal = left.type == right.type;
            if (equal && left.type == Value::INTEGER) { equal = left.integer == right.integer; }
            if (equal && left.type == Value::STRING) { equal = !strcmp(left.string, right.string); }
            return Value::from_integer(m_operation == Token::EQUAL_EQUAL ? equal : !equal);
        }
        case Token::PLUS:
            // Adding a string to anything joins them, so "x = " + 3 is "x = 3".
            if (left.type == Value::STRING || right.type == Value::STRING)
            {
                return Value::from_string(interpreter.concatenate(interpreter.to_string(left), interpreter.to_string(right)));
            }
            break;
        default:
            break;
        }

        if (left.type != Value::INTEGER || right.type != Value::INTEGER)
        {
            interpreter.error(m_line, "the operands of an arithmetic or comparison operator must be integers.");
            return Value();
        }
        if ((m_operation == Token::SLASH || m_operation == Token::PERCENT) && !right.integer)
        {
            interpreter.error(m_line, "division by zero.");
            return Value();
        }

        switch (m_operation)
        {
        case Token::PLUS: return Value::from_integer(left.integer + right.integer);
        case Token::MINUS: return Value::from_integer(left.integer - right.integer);
        case Token::STAR: return Value::from_integer(left.integer * right.integer);
        case Token::SLASH: return Value::from_integer(left.integer / right.integer);
        case Token::PERCENT: return Value::from_integer(left.integer % right.integer);
        case Token::LESS: return Value::from_integer(left.integer < right.integer);
        case Token::LESS_EQUAL: return Value::from_integer(left.integer <= right.integer);
        case Token::GREATER: return Value::from_integer(left.integer > right.integer);
        case Token::GREATER_EQUAL: return Value::from_integer(left.integer >= right.integer);
        default: return Value();
        }
    }

    CallExpression::~CallExpression()
    {
        delete[] m_name;
        for (auto it = m_arguments.first(); !it.is_end(); ++it) { delete *it; }
    }

    Value CallExpression::evaluate(Interpreter& interpreter)
    {
        Common::DoublyLinkedList<Value> arguments;
        for (auto it = m_arguments.first(); !it.is_end(); ++it)
        {
            arguments.append((*it)->evaluate(interpreter));
            if (interpreter.has_error()) { return Value(); }
        }
        return interpreter.call(m_name, arguments, m_line);
    }

    Statement::Result ExpressionStatement::execute(Interpreter& interpreter)
    {
        m_expression->evaluate(interpreter);
        return interpreter.has_error() ? Statement::ERROR : Statement::NORMAL;
    }

    Statement::Result VarStatement::execute(Interpreter& interpreter)
    {
        Value value;
        if (m_initialiser) { value = m_initialiser->evaluate(interpreter); }
        if (interpreter.has_error()) { return Statement::ERROR; }
        interpreter.define_variable(m_name, value);
        return Statement::NORMAL;
    }

    BlockStatement::~BlockStatement()
    {
        for (auto it = m_statements.first(); !it.is_end(); ++it) { delete *it; }
    }

    // Blocks do not create a new scope, so a variable belongs to the function it is declared in.
    Statement::Result BlockStatement::execute(Interpreter& interpreter)
    {
        for (auto it = m_statements.first(); !it.is_end(); ++it)
        {
            auto result = (*it)->execute(interpreter);
            if (result != Statement::NORMAL) { return result; }
        }
        return Statement::NORMAL;
    }

    Statement::Result IfStatement::execute(Interpreter& interpreter)
    {
        auto condition = m_condition->evaluate(interpreter);
        if (interpreter.has_error()) { return Statement::ERROR; }
        if (condition.is_true()) { return m_thenBranch->execute(interpreter); }
        if (m_elseBranch) { return m_elseBranch->execute(interpreter); }
        return Statement::NORMAL;
    }

    Statement::Result WhileStatement::execute(Interpreter& interpreter)
    {
        while (1)
        {
            // A loop may never end, so we check for Escape on every iteration.
            if (interpreter.is_escape_pressed())
            {
                interpreter.error(m_line, "stopped by Escape.");
                return Statement::ERROR;
            }
            auto condition = m_condition->evaluate(interpreter);
            if (interpreter.has_error()) { return Statement::ERROR; }
            if (!condition.is_true()) { return Statement::NORMAL; }

            auto result = m_body->execute(interpreter);
            if (result != Statement::NORMAL) { return result; }
        }
    }

    Statement::Result ReturnStatement::execute(Interpreter& interpreter)
    {
        Value value;
        if (m_value) { value = m_value->evaluate(interpreter); }
        if (interpreter.has_error()) { return Statement::ERROR; }
        interpreter.set_return_value(value);
        return Statement::RETURN;
    }

    FunctionStatement::~FunctionStatement()
    {
        delete[] m_name;
        for (auto it = m_parameters.first(); !it.is_end(); ++it) { delete[] *it; }
        delete m_body;
    }

    Statement::Result FunctionStatement::execute(Interpreter& interpreter)
    {
        interpreter.define_function(this);
        return Statement::NORMAL;
    }
}
