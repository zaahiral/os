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
    -I${CMAKE_CURRENT_SOURCE_DIR}/include/kernel -I${CMAKE_CURRENT_SOURCE_DIR}/userspace/include -Wl,-Ttext=0x400100 -Wl,-e,_start)
file(GLOB USERSPACE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/userspace/*.cpp)
file(GLOB USERSPACE_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/userspace/include/*.h)
set(USERSPACE_PROGRAMS)
foreach(SOURCE ${USERSPACE_SOURCES})
    get_filename_component(PROGRAM ${SOURCE} NAME_WE)
    add_custom_command(OUTPUT initrd/bin/${PROGRAM}
        COMMAND ${CMAKE_COMMAND} -E make_directory initrd/bin
        COMMAND ${CMAKE_CXX_COMPILER} ${USERSPACE_FLAGS} ${SOURCE} -o initrd/bin/${PROGRAM}
        DEPENDS ${SOURCE} ${USERSPACE_HEADERS})
    list(APPEND USERSPACE_PROGRAMS initrd/bin/${PROGRAM})
endforeach()

# The initrd is a USTAR file loaded as a Multiboot-2 module. The kernel requires modules to
# start and end on a page boundary, so we use a blocking factor of 8 (8 * 512 = 4KiB).
# Files are stored under initrd/, so they are opened as e.g. "initrd/hello.txt".
set(TARGET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/targets/x86_64)
file(GLOB_RECURSE INITRD_FILES CONFIGURE_DEPENDS RELATIVE ${TARGET_DIR} ${TARGET_DIR}/initrd/*)
list(TRANSFORM INITRD_FILES PREPEND ${TARGET_DIR}/ OUTPUT_VARIABLE INITRD_DEPENDS)
# The compiler that runs on cOS, from toolchain/build-cos-native.sh, is large, so it is only added to the initrd
# when its directory is given, as in cmake -DNATIVE_TOOLCHAIN=<directory>.
set(NATIVE_ARGUMENTS)
if(NATIVE_TOOLCHAIN)
    get_filename_component(NATIVE_TOOLCHAIN ${NATIVE_TOOLCHAIN} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    file(GLOB_RECURSE NATIVE_FILES CONFIGURE_DEPENDS RELATIVE ${NATIVE_TOOLCHAIN} ${NATIVE_TOOLCHAIN}/initrd/*)
    list(TRANSFORM NATIVE_FILES PREPEND ${NATIVE_TOOLCHAIN}/ OUTPUT_VARIABLE NATIVE_DEPENDS)
    list(APPEND INITRD_DEPENDS ${NATIVE_DEPENDS})
    set(NATIVE_ARGUMENTS -C ${NATIVE_TOOLCHAIN} ${NATIVE_FILES})
endif()
add_custom_command(OUTPUT initrd.tar
    COMMAND ${CMAKE_COMMAND} -E env COPYFILE_DISABLE=1 tar --format=ustar -b 8 -cf ${CMAKE_CURRENT_BINARY_DIR}/initrd.tar
        -C ${TARGET_DIR} ${INITRD_FILES} -C ${CMAKE_CURRENT_BINARY_DIR} ${USERSPACE_PROGRAMS} ${NATIVE_ARGUMENTS}
    DEPENDS ${INITRD_DEPENDS} ${USERSPACE_PROGRAMS})

# The disk is a 64MiB FAT32 image, made with mtools from the files in targets/x86_64/disk.
# It is only rebuilt when those files change, so files that cOS writes to it are kept between runs.
find_program(MFORMAT mformat REQUIRED)
find_program(MCOPY mcopy REQUIRED)
file(GLOB DISK_FILES CONFIGURE_DEPENDS ${TARGET_DIR}/disk/*)
add_custom_command(OUTPUT disk.img
    COMMAND ${CMAKE_COMMAND} -E rm -f disk.img
    COMMAND ${MFORMAT} -i disk.img -C -F -T 131072 -h 64 -s 32 ::
    COMMAND ${MCOPY} -i disk.img ${DISK_FILES} ::
    DEPENDS ${DISK_FILES})
add_custom_target(disk ALL DEPENDS disk.img)

find_program(GRUB_MKRESCUE NAMES grub-mkrescue i686-elf-grub-mkrescue REQUIRED)
add_custom_command(OUTPUT cOS.iso
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/targets/x86_64/iso iso
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kernel.bin> iso/boot/kernel.bin
    COMMAND ${CMAKE_COMMAND} -E copy initrd.tar iso/boot/initrd.tar
    COMMAND ${GRUB_MKRESCUE} -o cOS.iso iso
    DEPENDS kernel.bin initrd.tar ${CMAKE_CURRENT_SOURCE_DIR}/targets/x86_64/iso/boot/grub/grub.cfg)
add_custom_target(iso ALL DEPENDS cOS.iso)

# The UEFI loader is a freestanding PE application, built with the same GNU cross-toolchain.
find_program(UEFI_OBJCOPY NAMES x86_64-elf-objcopy REQUIRED)
find_program(MMD mmd REQUIRED)
add_custom_command(OUTPUT BOOTX64.EFI
    COMMAND ${CMAKE_CXX_COMPILER} -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fpie
        -fshort-wchar -mabi=ms -mno-red-zone -mno-sse -mno-sse2 -fno-stack-protector
        -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident
        -I${CMAKE_CURRENT_SOURCE_DIR}/include/kernel
        -c ${CMAKE_CURRENT_SOURCE_DIR}/boot/uefi/main.c -o uefi.o
    COMMAND ${UEFI_OBJCOPY} --remove-section=.comment uefi.o
    COMMAND ${CMAKE_LINKER} -mi386pep --subsystem 10 --entry efi_main --image-base 0x10000000
        --enable-reloc-section --no-insert-timestamp -o BOOTX64.EFI uefi.o
    DEPENDS boot/uefi/main.c boot/uefi/efi.h include/kernel/multiboot2.h)
add_custom_command(OUTPUT cOS-uefi.img
    COMMAND ${CMAKE_COMMAND} -E rm -f cOS-uefi.img
    COMMAND ${MFORMAT} -i cOS-uefi.img -C -F -T 262144 -h 64 -s 32 ::
    COMMAND ${MMD} -i cOS-uefi.img ::/EFI ::/EFI/BOOT
    COMMAND ${MCOPY} -i cOS-uefi.img BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
    COMMAND ${MCOPY} -i cOS-uefi.img $<TARGET_FILE:kernel.bin> ::/kernel.bin
    COMMAND ${MCOPY} -i cOS-uefi.img initrd.tar ::/initrd.tar
    DEPENDS BOOTX64.EFI kernel.bin initrd.tar)
add_custom_target(uefi DEPENDS cOS-uefi.img)

find_file(OVMF_FIRMWARE NAMES edk2-x86_64-code.fd OVMF_CODE.fd OVMF_CODE_4M.fd
    PATHS /opt/homebrew/share/qemu /usr/local/share/qemu /usr/share/qemu
        /usr/share/OVMF /usr/share/edk2/ovmf
    DOC "OVMF x86-64 firmware for the run-uefi target")
get_filename_component(OVMF_DIRECTORY "${OVMF_FIRMWARE}" DIRECTORY)
get_filename_component(OVMF_FILENAME "${OVMF_FIRMWARE}" NAME)
if(OVMF_FILENAME STREQUAL "OVMF_CODE_4M.fd")
    set(OVMF_VARIABLE_NAMES OVMF_VARS_4M.fd)
elseif(OVMF_FILENAME STREQUAL "edk2-x86_64-code.fd")
    # QEMU distributes a shared variables template for its i386 and x86-64 firmware.
    set(OVMF_VARIABLE_NAMES edk2-i386-vars.fd edk2-x86_64-vars.fd)
else()
    set(OVMF_VARIABLE_NAMES OVMF_VARS.fd)
endif()
find_file(OVMF_VARIABLES NAMES ${OVMF_VARIABLE_NAMES}
    HINTS ${OVMF_DIRECTORY}
    DOC "OVMF variables template matching OVMF_FIRMWARE")
if(OVMF_FIRMWARE AND OVMF_VARIABLES)
    add_custom_command(OUTPUT uefi-vars.fd
        COMMAND ${CMAKE_COMMAND} -E copy ${OVMF_VARIABLES} uefi-vars.fd
        DEPENDS ${OVMF_VARIABLES})
    add_custom_target(uefi-vars DEPENDS uefi-vars.fd)
    add_custom_target(run-uefi
        COMMAND qemu-system-x86_64 -m 1G -nic user,model=pcnet
            -drive if=pflash,format=raw,readonly=on,file=${OVMF_FIRMWARE}
            -drive if=pflash,format=raw,file=uefi-vars.fd
            -drive file=disk.img,format=raw,if=ide,index=0,media=disk
            -drive file=cOS-uefi.img,format=raw,if=ide,index=1,media=disk
        DEPENDS uefi disk uefi-vars
        USES_TERMINAL)
endif()

# QEMU with the network card the kernel has a driver for (AMD PCnet-PCI II), and the disk as the primary master.
# The disk is not bootable, so QEMU is told to boot from the CD.
add_custom_target(run
    COMMAND qemu-system-x86_64 -cdrom cOS.iso -m 1G -nic user,model=pcnet -drive file=disk.img,format=raw,if=ide,index=0,media=disk -boot d
    DEPENDS iso disk
    USES_TERMINAL)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
# The source disk is separate so a normal run keeps its small data disk and any files written there.
file(GLOB_RECURSE SELFHOST_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/*)
set(SELFHOST_STANDARD_FLAG ${CMAKE_CXX${CMAKE_CXX_STANDARD}_EXTENSION_COMPILE_OPTION})
if(DEFINED CMAKE_CXX_EXTENSIONS AND NOT CMAKE_CXX_EXTENSIONS)
    set(SELFHOST_STANDARD_FLAG ${CMAKE_CXX${CMAKE_CXX_STANDARD}_STANDARD_COMPILE_OPTION})
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" SELFHOST_CONFIGURATION)
add_custom_command(OUTPUT selfhost.img
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/toolchain/selfhost-disk.py
        --source ${CMAKE_CURRENT_SOURCE_DIR} --output ${CMAKE_CURRENT_BINARY_DIR}/selfhost.img
        --flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${SELFHOST_CONFIGURATION}} ${SELFHOST_STANDARD_FLAG}" ${KERNEL_SOURCES}
    DEPENDS ${KERNEL_SOURCES} ${SELFHOST_HEADERS} toolchain/selfhost-disk.py toolchain/selfbuild.c ${LINKER_SCRIPT}
    VERBATIM)
add_custom_target(selfhost-disk DEPENDS selfhost.img)
set(SELFHOST_BUILT_DISK ${CMAKE_CURRENT_BINARY_DIR}/selfhost-run.img CACHE FILEPATH "Disk containing the kernel built inside cOS")
add_custom_target(selfhost-iso
    COMMAND ${MCOPY} -o -i ${SELFHOST_BUILT_DISK} ::kernel.bin kernel-selfhost.bin
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${TARGET_DIR}/iso selfhost-iso
    COMMAND ${CMAKE_COMMAND} -E copy kernel-selfhost.bin selfhost-iso/boot/kernel.bin
    COMMAND ${CMAKE_COMMAND} -E copy initrd.tar selfhost-iso/boot/initrd.tar
    COMMAND ${GRUB_MKRESCUE} -o selfhost.iso selfhost-iso
    DEPENDS initrd.tar
    VERBATIM)
set(CHECK_ARGUMENTS --output ${CMAKE_CURRENT_BINARY_DIR}/check)
if(NATIVE_TOOLCHAIN)
    list(APPEND CHECK_ARGUMENTS --native-toolchain ${NATIVE_TOOLCHAIN})
endif()
add_custom_target(check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run.py ${CHECK_ARGUMENTS}
    USES_TERMINAL
    VERBATIM)
