#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "types.h"
#include "script/ast.h"
#include "common/doublyLinkedList.h"
#include "common/pair.h"

namespace Script
{
    #define SCRIPT_MAX_CALL_DEPTH 64

    // Runs cScript programs by walking their syntax trees. A running script is stopped by pressing Escape.
    class Interpreter
    {
    public:
        Interpreter(uint64_t tid, uint64_t keyboardEventDescriptor);
        ~Interpreter();
        // Runs a program, and prints the first error with its line number if there is one.
        void run(const char* source);

        // These are called by the syntax tree while it is executed.
        Value get_variable(const char* name, uint64_t line);
        void define_variable(const char* name, Value value);
        void assign_variable(const char* name, Value value, uint64_t line);
        void define_function(FunctionStatement* function);
        Value call(const char* name, Common::DoublyLinkedList<Value>& arguments, uint64_t line);
        void set_return_value(Value value) { m_returnValue = value; }
        const char* to_string(Value value);
        const char* concatenate(const char* left, const char* right);
        // Prints an error with an optional detail, such as a variable name, and stops the program.
        void error(uint64_t line, const char* message, const char* detail = nullptr);
        bool has_error() { return m_hasError; }
        bool is_escape_pressed();
    private:
        struct Scope
        {
            Common::DoublyLinkedList<Common::Pair<const char*, Value>> variables;
            Scope* parent = nullptr;
        };

        uint64_t m_tid;
        uint64_t m_keyboardEventDescriptor;
        bool m_hasError = false;
        uint64_t m_callDepth = 0;
        Value m_returnValue;
        Scope m_globalScope;
        Scope* m_currentScope;
        Common::DoublyLinkedList<FunctionStatement*> m_functions;
        // Strings made while the program runs, which are freed when the interpreter is destroyed.
        Common::DoublyLinkedList<char*> m_strings;
        char* make_string(uint64_t length);
        Value* find_variable(const char* name);
        bool call_builtin(const char* name, Common::DoublyLinkedList<Value>& arguments, uint64_t line, Value& result);
    };
}

#endif
