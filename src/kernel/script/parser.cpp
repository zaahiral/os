#include "script/parser.h"
#include "script/interpreter.h"
#include "libc/string.h"

namespace Script
{
    void Parser::advance()
    {
        m_previous = m_current;
        m_current = m_lexer.next_token();
        if (m_current.type == Token::ERROR) { error(m_current.start, m_current.line); }
    }

    bool Parser::match(Token::Type type)
    {
        if (!check(type)) { return false; }
        advance();
        return true;
    }

    // A missing token belongs after the previous token, so the error is reported on the previous token's line.
    bool Parser::consume(Token::Type type, const char* message)
    {
        if (match(type)) { return true; }
        error(message, m_previous.line);
        return false;
    }

    void Parser::error(const char* message, uint64_t line)
    {
        if (m_hasError) { return; }
        m_hasError = true;
        m_interpreter.error(line, message);
    }

    char* Parser::copy_identifier(Token token)
    {
        auto text = new char[token.length + 1];
        for (uint64_t i = 0; i < token.length; i++) { text[i] = token.start[i]; }
        text[token.length] = '\0';
        return text;
    }

    // String tokens include their quotes, which we remove, and we replace escape sequences such as \n.
    char* Parser::copy_string(Token token)
    {
        auto text = new char[token.length - 1];
        uint64_t length = 0;
        for (uint64_t i = 1; i < token.length - 1; i++)
        {
            char c = token.start[i];
            if (c == '\\')
            {
                c = token.start[++i];
                if (c == 'n') { c = '\n'; }
                else if (c == 't') { c = '\t'; }
            }
            text[length++] = c;
        }
        text[length] = '\0';
        return text;
    }

    // Joins two operands with a binary operator, or frees them if either of them failed to parse.
    Expression* Parser::make_binary(Token operation, Expression* left, Expression* right)
    {
        if (!left || !right)
        {
            delete left;
            delete right;
            return nullptr;
        }
        return new BinaryExpression(operation.line, operation.type, left, right);
    }

    BlockStatement* Parser::parse_program()
    {
        auto program = new BlockStatement(1);
        advance();
        while (!check(Token::END) && !m_hasError)
        {
            auto statement = declaration();
            if (!statement) { break; }
            program->add_statement(statement);
        }
        if (m_hasError)
        {
            delete program;
            return nullptr;
        }
        return program;
    }

    Statement* Parser::declaration()
    {
        if (match(Token::VAR)) { return var_declaration(); }
        if (match(Token::FUNC)) { return function_declaration(); }
        return statement();
    }

    Statement* Parser::var_declaration()
    {
        uint64_t line = m_previous.line;
        if (!consume(Token::IDENTIFIER, "expected a variable name after var.")) { return nullptr; }
        auto name = copy_identifier(m_previous);

        Expression* initialiser = nullptr;
        if (match(Token::EQUAL))
        {
            initialiser = expression();
            if (!initialiser) { delete[] name; return nullptr; }
        }
        auto statement = new VarStatement(line, name, initialiser);
        if (!consume(Token::SEMICOLON, "expected ';' after a variable declaration.")) { delete statement; return nullptr; }
        return statement;
    }

    Statement* Parser::function_declaration()
    {
        uint64_t line = m_previous.line;
        if (!consume(Token::IDENTIFIER, "expected a function name after func.")) { return nullptr; }
        auto function = new FunctionStatement(line, copy_identifier(m_previous), nullptr);

        if (!consume(Token::LEFT_PAREN, "expected '(' after the function name.")) { delete function; return nullptr; }
        if (!check(Token::RIGHT_PAREN))
        {
            do
            {
                if (!consume(Token::IDENTIFIER, "expected a parameter name.")) { delete function; return nullptr; }
                function->add_parameter(copy_identifier(m_previous));
            }
            while (match(Token::COMMA));
        }
        if (!consume(Token::RIGHT_PAREN, "expected ')' after the parameters.")
            || !consume(Token::LEFT_BRACE, "expected '{' before the function body."))
        {
            delete function;
            return nullptr;
        }

        auto body = block();
        if (!body) { delete function; return nullptr; }
        function->set_body(body);
        return function;
    }

    Statement* Parser::statement()
    {
        if (match(Token::IF)) { return if_statement(); }
        if (match(Token::WHILE)) { return while_statement(); }
        if (match(Token::RETURN)) { return return_statement(); }
        if (match(Token::LEFT_BRACE)) { return block(); }
        return expression_statement();
    }

    Statement* Parser::if_statement()
    {
        uint64_t line = m_previous.line;
        if (!consume(Token::LEFT_PAREN, "expected '(' after if.")) { return nullptr; }
        auto condition = expression();
        if (!condition) { return nullptr; }
        if (!consume(Token::RIGHT_PAREN, "expected ')' after the condition.")) { delete condition; return nullptr; }

        auto thenBranch = statement();
        if (!thenBranch) { delete condition; return nullptr; }
        Statement* elseBranch = nullptr;
        if (match(Token::ELSE))
        {
            elseBranch = statement();
            if (!elseBranch) { delete condition; delete thenBranch; return nullptr; }
        }
        return new IfStatement(line, condition, thenBranch, elseBranch);
    }

    Statement* Parser::while_statement()
    {
        uint64_t line = m_previous.line;
        if (!consume(Token::LEFT_PAREN, "expected '(' after while.")) { return nullptr; }
        auto condition = expression();
        if (!condition) { return nullptr; }
        if (!consume(Token::RIGHT_PAREN, "expected ')' after the condition.")) { delete condition; return nullptr; }

        auto body = statement();
        if (!body) { delete condition; return nullptr; }
        return new WhileStatement(line, condition, body);
    }

    Statement* Parser::return_statement()
    {
        uint64_t line = m_previous.line;
        Expression* value = nullptr;
        if (!check(Token::SEMICOLON))
        {
            value = expression();
            if (!value) { return nullptr; }
        }
        auto statement = new ReturnStatement(line, value);
        if (!consume(Token::SEMICOLON, "expected ';' after return.")) { delete statement; return nullptr; }
        return statement;
    }

    Statement* Parser::expression_statement()
    {
        uint64_t line = m_current.line;
        auto value = expression();
        if (!value) { return nullptr; }
        auto statement = new ExpressionStatement(line, value);
        if (!consume(Token::SEMICOLON, "expected ';' after an expression.")) { delete statement; return nullptr; }
        return statement;
    }

    // The opening brace has already been consumed.
    BlockStatement* Parser::block()
    {
        auto block = new BlockStatement(m_previous.line);
        while (!check(Token::RIGHT_BRACE) && !check(Token::END))
        {
            auto statement = declaration();
            if (!statement) { delete block; return nullptr; }
            block->add_statement(statement);
        }
        if (!consume(Token::RIGHT_BRACE, "expected '}' after a block.")) { delete block; return nullptr; }
        return block;
    }

    Expression* Parser::expression() { return assignment(); }

    Expression* Parser::assignment()
    {
        auto target = logical_or();
        if (!target || !match(Token::EQUAL)) { return target; }

        uint64_t line = m_previous.line;
        auto value = assignment();
        if (!value) { delete target; return nullptr; }
        if (!target->get_variable_name())
        {
            error("only a variable can be assigned to.", line);
            delete target;
            delete value;
            return nullptr;
        }
        // The assignment takes its own copy of the name, as the variable expression is freed.
        auto name = target->get_variable_name();
        auto nameCopy = copy_identifier(Token{Token::IDENTIFIER, name, strlen(name), line});
        delete target;
        return new AssignExpression(line, nameCopy, value);
    }

    Expression* Parser::logical_or()
    {
        auto expression = logical_and();
        while (expression && match(Token::OR))
        {
            auto operation = m_previous;
            expression = make_binary(operation, expression, logical_and());
        }
        return expression;
    }

    Expression* Parser::logical_and()
    {
        auto expression = equality();
        while (expression && match(Token::AND))
        {
            auto operation = m_previous;
            expression = make_binary(operation, expression, equality());
        }
        return expression;
    }

    Expression* Parser::equality()
    {
        auto expression = comparison();
        while (expression && (match(Token::EQUAL_EQUAL) || match(Token::BANG_EQUAL)))
        {
            auto operation = m_previous;
            expression = make_binary(operation, expression, comparison());
        }
        return expression;
    }

    Expression* Parser::comparison()
    {
        auto expression = term();
        while (expression && (match(Token::LESS) || match(Token::LESS_EQUAL)
            || match(Token::GREATER) || match(Token::GREATER_EQUAL)))
        {
            auto operation = m_previous;
            expression = make_binary(operation, expression, term());
        }
        return expression;
    }

    Expression* Parser::term()
    {
        auto expression = factor();
        while (expression && (match(Token::PLUS) || match(Token::MINUS)))
        {
            auto operation = m_previous;
            expression = make_binary(operation, expression, factor());
        }
        return expression;
    }

    Expression* Parser::factor()
    {
        auto expression = unary();
        while (expression && (match(Token::STAR) || match(Token::SLASH) || match(Token::PERCENT)))
        {
            auto operation = m_previous;
            expression = make_binary(operation, expression, unary());
        }
        return expression;
    }

    Expression* Parser::unary()
    {
        if (match(Token::BANG) || match(Token::MINUS))
        {
            auto operation = m_previous;
            auto operand = unary();
            if (!operand) { return nullptr; }
            return new UnaryExpression(operation.line, operation.type, operand);
        }
        return primary();
    }

    Expression* Parser::primary()
    {
        uint64_t line = m_current.line;
        if (match(Token::NUMBER))
        {
            int64_t number = 0;
            for (uint64_t i = 0; i < m_previous.length; i++) { number = number * 10 + (m_previous.start[i] - '0'); }
            return new NumberExpression(line, number);
        }
        if (match(Token::STRING)) { return new StringExpression(line, copy_string(m_previous)); }
        if (match(Token::IDENTIFIER))
        {
            auto name = copy_identifier(m_previous);
            if (!match(Token::LEFT_PAREN)) { return new VariableExpression(line, name); }

            // An identifier followed by a bracket is a function call.
            auto call = new CallExpression(line, name);
            if (!check(Token::RIGHT_PAREN))
            {
                do
                {
                    auto argument = expression();
                    if (!argument) { delete call; return nullptr; }
                    call->add_argument(argument);
                }
                while (match(Token::COMMA));
            }
            if (!consume(Token::RIGHT_PAREN, "expected ')' after the arguments.")) { delete call; return nullptr; }
            return call;
        }
        if (match(Token::LEFT_PAREN))
        {
            auto inner = expression();
            if (!inner) { return nullptr; }
            if (!consume(Token::RIGHT_PAREN, "expected ')' after the expression.")) { delete inner; return nullptr; }
            return inner;
        }
        error("expected an expression.", m_current.line);
        return nullptr;
    }
}
