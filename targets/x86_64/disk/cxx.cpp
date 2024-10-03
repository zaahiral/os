#include <array>
#include <cstdint>
#include <type_traits>

constexpr auto message = std::to_array("Hello from C++ compiled on cOS!\n");
static_assert(std::is_integral_v<std::uint64_t> && sizeof(std::uint64_t) == 8);

// The freestanding example uses system calls directly because it has no C library startup code.
extern "C" void _start()
{
    asm volatile("syscall" : : "a"(1ULL), "D"(message.data()) : "rcx", "r11", "memory");
    asm volatile("syscall" : : "a"(14ULL), "D"(0ULL) : "rcx", "r11", "memory");
    __builtin_unreachable();
}
