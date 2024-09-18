#include "script/interpreter.h"
#include "script/parser.h"
#include "print.h"
#include "libc/string.h"
#include "memory/kheap.h"
#include "cpu/io/pit.h"
#include "fs/vfs.h"
#include "event/dispatcher.h"
#include "process/taskManager.h"

namespace Script
{
    namespace
    {
        constexpr uint64_t ESCAPE_SCAN_CODE = 0x01;
    }

    Interpreter::Interpreter(uint64_t tid, uint64_t keyboardEventDescriptor) : m_tid(tid), m_keyboardEventDescriptor(keyboardEventDescriptor)
    {
        m_currentScope = &m_globalScope;
    }

    Interpreter::~Interpreter()
    {
        for (auto it = m_strings.first(); !it.is_end(); ++it) { delete[] *it; }
    }

    void Interpreter::run(const char* source)
    {
        Parser parser(source, *this);
        auto program = parser.parse_program();
        if (!program) { return; }
        program->execute(*this);
        delete program;
    }

    void Interpreter::error(uint64_t line, const char* message, const char* detail)
    {
        // Only the first error is printed, as later errors are usually caused by it.
        if (m_hasError) { return; }
        m_hasError = true;
        printf("Error on line ");
        printf(line);
        printf(": ");
        printf(message);
        if (detail)
        {
            printf(detail);
            printf(".");
        }
        printf("\n");
    }

    // Read every key pressed since the last check, so that a long running script can be stopped.
    bool Interpreter::is_escape_pressed()
    {
        uint64_t scanCode;
        while ((scanCode = Events::EventDispatcher::instance().read_from_event_queue(m_tid, m_keyboardEventDescriptor)))
        {
            if (scanCode == ESCAPE_SCAN_CODE) { return true; }
        }
        return false;
    }

    char* Interpreter::make_string(uint64_t length)
    {
        auto string = new char[length + 1];
        m_strings.append(string);
        return string;
    }

    const char* Interpreter::concatenate(const char* left, const char* right)
    {
        auto leftLength = strlen(left);
        auto string = make_string(leftLength + strlen(right));
        strcpy(string, (char*)left);
        strcpy(string + leftLength, (char*)right);
        return string;
    }

    const char* Interpreter::to_string(Value value)
    {
        if (value.type == Value::STRING) { return value.string; }
        if (value.type == Value::NONE) { return "none"; }

        // We work with the magnitude as an unsigned number, so the most negative integer is also printed correctly.
        bool isNegative = value.integer < 0;
        uint64_t magnitude = isNegative ? -(uint64_t)value.integer : value.integer;
        char digits[21];
        uint64_t count = 0;
        do
        {
            digits[count++] = '0' + magnitude % 10;
            magnitude /= 10;
        }
        while (magnitude);

        auto string = make_string(count + isNegative);
        uint64_t length = 0;
        if (isNegative) { string[length++] = '-'; }
        while (count) { string[length++] = digits[--count]; }
        string[length] = '\0';
        return string;
    }

    // Variables are found in the current function's scope first, then in the global scope.
    Value* Interpreter::find_variable(const char* name)
    {
        for (auto scope = m_currentScope; scope; scope = scope->parent)
        {
            for (auto it = scope->variables.first(); !it.is_end(); ++it)
            {
                if (!strcmp(it->first, name)) { return &it->last; }
            }
        }
        return nullptr;
    }

    Value Interpreter::get_variable(const char* name, uint64_t line)
    {
        auto variable = find_variable(name);
        if (!variable)
        {
            error(line, "undefined variable ", name);
            return Value();
        }
        return *variable;
    }

    void Interpreter::define_variable(const char* name, Value value)
    {
        for (auto it = m_currentScope->variables.first(); !it.is_end(); ++it)
        {
            if (!strcmp(it->first, name)) { it->last = value; return; }
        }
        m_currentScope->variables.append(Common::Pair<const char*, Value>(name, value));
    }

    void Interpreter::assign_variable(const char* name, Value value, uint64_t line)
    {
        auto variable = find_variable(name);
        if (!variable)
        {
            error(line, "undefined variable ", name);
            return;
        }
        *variable = value;
    }

    void Interpreter::define_function(FunctionStatement* function)
    {
        for (auto it = m_functions.first(); !it.is_end(); ++it)
        {
            if (!strcmp((*it)->get_name(), function->get_name())) { *it = function; return; }
        }
        m_functions.append(function);
    }

    bool Interpreter::call_builtin(const char* name, Common::DoublyLinkedList<Value>& arguments, uint64_t line, Value& result)
    {
        if (!strcmp(name, "print"))
        {
            for (auto it = arguments.first(); !it.is_end(); ++it) { printf(to_string(*it)); }
            printf("\n");
            return true;
        }
        if (!strcmp(name, "uptime"))
        {
            result = Value::from_integer(CPU::PIT::time_since_boot());
            return true;
        }

        // The remaining built-in functions take exactly one argument.
        if (strcmp(name, "sleep") && strcmp(name, "length") && strcmp(name, "read")) { return false; }
        if (arguments.size() != 1)
        {
            error(line, "expected one argument for ", name);
            return true;
        }
        auto argument = *arguments.first();

        if (!strcmp(name, "sleep"))
        {
            if (argument.type != Value::INTEGER) { error(line, "sleep takes a number of milliseconds."); return true; }
            Task::TaskManager::instance().sleep_for(argument.integer);
            return true;
        }
        if (argument.type != Value::STRING)
        {
            error(line, "expected a string for ", name);
            return true;
        }
        if (!strcmp(name, "length"))
        {
            result = Value::from_integer(strlen(argument.string));
            return true;
        }

        // read returns the contents of a file in the initrd as a string.
        auto file = Filesystem::VirtualFilesystemManager::instance().open_file((char*)argument.string, 0);
        if (!file || !(file->flags & Filesystem::VirtualFilesystemNode::VFS_FILE))
        {
            if (file) { delete file; }
            error(line, "no such file ", argument.string);
            return true;
        }
        auto length = Filesystem::VirtualFilesystemManager::instance().filelen(file);
        auto contents = make_string(length);
        Filesystem::VirtualFilesystemManager::instance().read(file, 0, length, contents);
        contents[length] = '\0';
        delete file;
        result = Value::from_string(contents);
        return true;
    }

    Value Interpreter::call(const char* name, Common::DoublyLinkedList<Value>& arguments, uint64_t line)
    {
        Value result;
        if (call_builtin(name, arguments, line, result)) { return result; }

        FunctionStatement* function = nullptr;
        for (auto it = m_functions.first(); !it.is_end(); ++it)
        {
            if (!strcmp((*it)->get_name(), name)) { function = *it; break; }
        }
        if (!function)
        {
            error(line, "undefined function ", name);
            return Value();
        }
        if (function->get_parameters().size() != arguments.size())
        {
            error(line, "wrong number of arguments for ", name);
            return Value();
        }
        if (m_callDepth == SCRIPT_MAX_CALL_DEPTH)
        {
            error(line, "too many nested calls in ", name);
            return Value();
        }

        // Each call gets a new scope for its parameters and variables, which can also see the global scope.
        Scope scope;
        scope.parent = &m_globalScope;
        auto argument = arguments.first();
        for (auto parameter = function->get_parameters().first(); !parameter.is_end(); ++parameter, ++argument)
        {
            scope.variables.append(Common::Pair<const char*, Value>(*parameter, *argument));
        }

        auto previousScope = m_currentScope;
        m_currentScope = &scope;
        m_callDepth++;
        m_returnValue = Value();
        auto executionResult = function->get_body()->execute(*this);
        m_callDepth--;
        m_currentScope = previousScope;

        if (executionResult == Statement::ERROR) { return Value(); }
        result = m_returnValue;
        m_returnValue = Value();
        return result;
    }
}
