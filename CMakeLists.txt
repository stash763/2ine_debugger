cmake_minimum_required(VERSION 3.4)
project(2ine)

# Note that this project doesn't work on Mac OS X because I can't mmap()
#  address 0x10000, which is the usual base address for OS/2 binaries.
# It appears the address is already accessible when main() begins, but no
#  objects seem to be loaded there. I don't know what it is...maybe some
#  thing allocated by the C runtime at startup? Have to solve that somehow or
#  give up on Mac support.
#
# Eventually, even on Linux, it might make sense to override malloc and friends
#  at the dynamic loader level, so we control all allocations even if they
#  happen in glibc. Once we have that, make sure they all sit in the upper
#  2 gigabytes (reserved for system use on OS/2, so this works out), probably
#  with them mmap()'d there via a local copy of dlmalloc or something.
#
# Note that 32-bit OS/2 programs only got 512 megabytes of the address space
#  (enormous in the days of machines with a couple megabytes of physical RAM!)
#  to accomodate thunking to and from 16 bit code. We don't bother with this
#  at the moment, though, but maybe we must at some point. I hope not.
#
# Also, it might be nice if we can run as a 64-bit process and thunk around in
#  32-bits as necessary, but I don't know the technical barriers to this yet.
#  If we can pull that off, it would allow an OS/2 app to have the full 4
#  gigabytes of memory, and we'll just put 2ine's data outside of that range,
#  and run the native bits with more registers, etc. Maybe even keep the
#  native functions out of low memory and only eat a few bytes of it with
#  trampolines. I'm probably getting ahead of myself, though, as it's not like
#  any OS/2 apps were _that_ memory hungry, and honestly? Port your software.

if(NOT CMAKE_BUILD_TYPE)
    set(DEBUG_BUILD ON)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_BUILD ON)
endif()

if(DEBUG_BUILD)
    add_definitions(-DDEBUG -O0)
else()
    add_definitions(-D_RELEASE)
    add_definitions(-DNDEBUG)
    add_definitions(-DRELEASE)
endif()

option(LX_LEGACY "Enable OS/2 binary support (vs just native apps)" TRUE)
if(NOT LX_LEGACY)
    add_definitions(-DLX_LEGACY=0)
endif()

add_definitions(-std=c99 -Wall -ggdb3)
add_definitions(-D_FILE_OFFSET_BITS=64)

set(CURSES_NEED_WIDE TRUE)
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses)
if(NOT CURSES_FOUND)
    message(FATAL_ERROR "ncurses not found")
endif()

include_directories(${CURSES_INCLUDE_DIR})
if(CURSES_HAVE_NCURSESW_NCURSES_H)
    add_definitions(-DHAVE_NCURSESW_NCURSES_H)
elseif(CURSES_HAVE_NCURSESW_CURSES_H)
    add_definitions(-DHAVE_NCURSESW_CURSES_H)
elseif(CURSES_HAVE_NCURSESW_H)
    add_definitions(-DHAVE_NCURSESW_H)
elseif(CURSES_HAVE_CURSES_H)
    add_definitions(-DHAVE_CURSES_H)
elseif(CURSES_HAVE_NCURSES_CURSES_H)
    add_definitions(-DHAVE_NCURSESW_NCURSES_H)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
include_directories(${SDL2_INCLUDE_DIRS})

# Capstone is vendored in capstone/ directory
set(CAPSTONE_BUILD_STATIC ON CACHE BOOL "Build Capstone as static library")
set(CAPSTONE_X86_SUPPORT ON CACHE BOOL "Enable x86 disassembly")
add_subdirectory(capstone)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/capstone/include)

option(BUILD_DEBUGGER "Build Turbo Debugger for 2ine" TRUE)

add_library(2ine SHARED "lib2ine.c")
target_link_libraries(2ine "pthread")
if(LX_LEGACY)
    set_target_properties(2ine PROPERTIES COMPILE_FLAGS "-m32")
    set_target_properties(2ine PROPERTIES LINK_FLAGS "-m32 -ggdb3")
endif()

# Build Turbo Vision (for debugger)
if(BUILD_DEBUGGER)
    option(TV_BUILD_EXAMPLES OFF)
    option(TV_BUILD_TESTS OFF)
    add_subdirectory(tvision)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/tvision/include)
endif()

foreach(_I doscalls;msg;nls;quecalls;viocalls;kbdcalls;sesmgr;som;pmwin;pmshapi;pmgpi;tcpip32)
    add_library(${_I} SHARED "native/${_I}.c")
    if(LX_LEGACY)
        set_target_properties(${_I} PROPERTIES COMPILE_FLAGS "-m32")
        set_target_properties(${_I} PROPERTIES LINK_FLAGS "-m32 -ggdb3")
    endif()
    target_link_libraries(${_I} 2ine)
endforeach()

target_link_libraries(doscalls "pthread")
# FIXME target_link_libraries(viocalls ${CURSES_LIBRARIES})
target_link_libraries(viocalls /usr/lib/i386-linux-gnu/libncursesw.so.6)

# !!! FIXME: clean this up/
if(LX_LEGACY)
    target_link_libraries(2ine ${SDL2_LIBRARIES})
    target_link_libraries(pmwin ${SDL2_LIBRARIES})
else()
    target_link_libraries(2ine "SDL2")
    target_link_libraries(pmwin "SDL2")
endif()

add_executable(lx_dump lx_dump.c)
target_include_directories(lx_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/capstone/include)
if(LX_LEGACY)
    set_target_properties(lx_dump PROPERTIES COMPILE_FLAGS "-m32")
    set_target_properties(lx_dump PROPERTIES LINK_FLAGS "-m32 -ggdb3")
    target_link_libraries(lx_dump capstone)
endif()

if(LX_LEGACY)
    add_executable(lx_loader lx_loader.c debugger/ldt_access.c)
    target_link_libraries(lx_loader 2ine)
    target_link_libraries(lx_loader "dl" "rt")
    set_target_properties(lx_loader PROPERTIES COMPILE_FLAGS "-m32 -no-pie")
    set_target_properties(lx_loader PROPERTIES LINK_FLAGS "-m32 -no-pie -ggdb3")
endif()

# Turbo Debugger for 2ine (Turbo Vision-based)
if(BUILD_DEBUGGER)
    enable_language(CXX)
    add_executable(td2ine
        debugger/td2ine_main.cpp
        debugger/td2ine_core.cpp
        debugger/td2ine_tui_views.cpp
        debugger/td2ine_tui_app.cpp
        debugger/td2ine_text.cpp
        debugger/ptrace_wrapper.c
        debugger/ldt_access.c
        debugger/disasm.c
    )
    target_include_directories(td2ine PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/capstone/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tvision/include
    )
    target_link_libraries(td2ine
        capstone
        tvision
        pthread
        rt
    )
    if(LX_LEGACY)
        set_target_properties(td2ine PROPERTIES COMPILE_FLAGS "-m32")
        set_target_properties(td2ine PROPERTIES LINK_FLAGS "-m32 -ggdb3")
        set_target_properties(td2ine PROPERTIES LINKER_LANGUAGE CXX)
        # Ensure 2ine and lx_loader are built first (needed for testing)
        add_dependencies(td2ine 2ine lx_loader)
    endif()
    
    # OS/2 program debug test
    add_executable(td_os2_test
        debugger/test_os2_debug.c
    )
    target_link_libraries(td_os2_test pthread rt)
    if(LX_LEGACY)
        set_target_properties(td_os2_test PROPERTIES COMPILE_FLAGS "-m32")
        set_target_properties(td_os2_test PROPERTIES LINK_FLAGS "-m32 -ggdb3")
        add_dependencies(td_os2_test 2ine lx_loader)
    endif()
    
    # Convenience target to build everything needed for debugging
    add_custom_target(debugger ALL
        DEPENDS td2ine 2ine lx_loader lx_dump
        COMMENT "Debugger and dependencies built successfully"
    )
endif()  # BUILD_DEBUGGER

# end of CMakeLists.txt ...

