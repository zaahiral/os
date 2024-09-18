cmake_minimum_required(VERSION 3.16)

# The toolchain file must be set before project() so CMake uses the cross-compiler.
if (NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/GNUToolchain.cmake)
endif()

project(cOS CXX ASM_NASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE KERNEL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/x86_64/*.asm)

add_executable(kernel.bin ${KERNEL_SOURCES})
target_include_directories(kernel.bin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/include/kernel/x86_64)
set_target_properties(kernel.bin PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

# User-space programs are compiled as static ELF executables into initrd/bin. They are linked at 0x400100,
# as the ELF loader copies the ELF header to 0x400000.
set(USERSPACE_FLAGS -ffreestanding -nostdlib -static -fno-pie -mno-red-zone -mno-sse -fno-exceptions -fno-rtti
    -I${CMAKE_CURRENT_SOURCE_DIR}/include/kernel -Wl,-Ttext=0x400100 -Wl,-e,_start)
file(GLOB USERSPACE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/userspace/*.cpp)
set(USERSPACE_PROGRAMS)
foreach(SOURCE ${USERSPACE_SOURCES})
    get_filename_component(PROGRAM ${SOURCE} NAME_WE)
    add_custom_command(OUTPUT initrd/bin/${PROGRAM}
        COMMAND ${CMAKE_COMMAND} -E make_directory initrd/bin
        COMMAND ${CMAKE_CXX_COMPILER} ${USERSPACE_FLAGS} ${SOURCE} -o initrd/bin/${PROGRAM}
        DEPENDS ${SOURCE})
    list(APPEND USERSPACE_PROGRAMS initrd/bin/${PROGRAM})
endforeach()

# The initrd is a USTAR file loaded as a Multiboot-2 module. The kernel requires modules to
# start and end on a page boundary, so we use a blocking factor of 8 (8 * 512 = 4KiB).
# Files are stored under initrd/, so they are opened as e.g. "initrd/hello.txt".
set(TARGET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/targets/x86_64)
file(GLOB_RECURSE INITRD_FILES CONFIGURE_DEPENDS RELATIVE ${TARGET_DIR} ${TARGET_DIR}/initrd/*)
list(TRANSFORM INITRD_FILES PREPEND ${TARGET_DIR}/ OUTPUT_VARIABLE INITRD_DEPENDS)
add_custom_command(OUTPUT initrd.tar
    COMMAND ${CMAKE_COMMAND} -E env COPYFILE_DISABLE=1 tar --format=ustar -b 8 -cf ${CMAKE_CURRENT_BINARY_DIR}/initrd.tar
        -C ${TARGET_DIR} ${INITRD_FILES} -C ${CMAKE_CURRENT_BINARY_DIR} ${USERSPACE_PROGRAMS}
    DEPENDS ${INITRD_DEPENDS} ${USERSPACE_PROGRAMS})

find_program(GRUB_MKRESCUE NAMES grub-mkrescue i686-elf-grub-mkrescue REQUIRED)
add_custom_command(OUTPUT cOS.iso
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/targets/x86_64/iso iso
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kernel.bin> iso/boot/kernel.bin
    COMMAND ${CMAKE_COMMAND} -E copy initrd.tar iso/boot/initrd.tar
    COMMAND ${GRUB_MKRESCUE} -o cOS.iso iso
    DEPENDS kernel.bin initrd.tar ${CMAKE_CURRENT_SOURCE_DIR}/targets/x86_64/iso/boot/grub/grub.cfg)
add_custom_target(iso ALL DEPENDS cOS.iso)

# QEMU with the network card the kernel has a driver for (AMD PCnet-PCI II).
add_custom_target(run
    COMMAND qemu-system-x86_64 -cdrom cOS.iso -m 256M -nic user,model=pcnet
    DEPENDS iso
    USES_TERMINAL)
