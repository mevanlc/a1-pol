cmake_minimum_required(VERSION 3.16)

project(a1-pol VERSION 0.1.0 LANGUAGES C)

add_executable(a1-pol-mem src/a1-pol-mem.c)
set_property(TARGET a1-pol-mem PROPERTY C_STANDARD 11)
set_property(TARGET a1-pol-mem PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET a1-pol-mem PROPERTY C_EXTENSIONS OFF)

if(MSVC)
  target_compile_options(a1-pol-mem PRIVATE /W4)
else()
  target_compile_options(a1-pol-mem PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  target_link_libraries(a1-pol-mem PRIVATE bcrypt)
endif()

install(TARGETS a1-pol-mem RUNTIME DESTINATION bin)
