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

# QEMU with the network card the kernel has a driver for (AMD PCnet-PCI II), and the disk as the primary master.
# The disk is not bootable, so QEMU is told to boot from the CD.
add_custom_target(run
    COMMAND qemu-system-x86_64 -cdrom cOS.iso -m 1G -nic user,model=pcnet -drive file=disk.img,format=raw,if=ide,index=0,media=disk -boot d
    DEPENDS iso disk
    USES_TERMINAL)
