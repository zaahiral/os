set(CMAKE_SYSTEM_NAME cOS)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-elf-)

set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)
set(extra_flags "-m elf_x86_64")

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER nasm)
set(CMAKE_LINKER ${TOOLCHAIN_PREFIX}ld)
set(CMAKE_C_FLAGS " -g -mno-red-zone -nostartfiles -nostdlib  -fno-sized-deallocation -fno-rtti -fno-exceptions -ffreestanding -mcmodel=large -mno-sse -lgcc" )
set(CMAKE_CXX_FLAGS " -g -mno-red-zone -nostartfiles -nostdlib  -fno-sized-deallocation -fno-rtti -fno-exceptions -ffreestanding -mcmodel=large -mno-sse -lgcc" )
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

set(LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../targets/x86_64/linker.ld")
set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> \
    <FLAGS> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")
set(CMAKE_ASM_NASM_LINK_EXECUTABLE "${TOOLCHAIN_PREFIX}ld -n -T ${LINKER_SCRIPT} <CMAKE_ASM_NASM_LINK_FLAGS> <LINK_FLAGS> <OBJECTS>  -o <TARGET> <LINK_LIBRARIES> <OBJECTS>")
set(CMAKE_CXX_LINK_EXECUTABLE "${TOOLCHAIN_PREFIX}ld -n -T ${LINKER_SCRIPT} -o <TARGET> <LINK_LIBRARIES> <OBJECTS>" )
