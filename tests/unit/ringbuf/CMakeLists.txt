cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ringbuf_tests)

set(APP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../app/src)

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
    ${APP_SOURCE_DIR}/ringbuf.c
)

target_include_directories(app PRIVATE
    ${APP_SOURCE_DIR}
)