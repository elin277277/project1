include(FetchContent)
# Eclipse ThreadX 6.4.1, fixed commit and archive digest. No user credentials needed.
FetchContent_Declare(threadx_source
    URL https://codeload.github.com/eclipse-threadx/threadx/tar.gz/07eac307405dbf09d409844e31c37b0649d6c074
    URL_HASH SHA256=823b20f64d644141b84b0ca8e2fe714fe1c5984fbdc74a92ab464515474ac942
    SOURCE_SUBDIR no_auto_cmake
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(threadx_source)
# Only the kernel and GNU Cortex-M4 port, not examples or compatibility layers.
file(GLOB TX_COMMON CONFIGURE_DEPENDS "${threadx_source_SOURCE_DIR}/common/src/*.c")
file(GLOB TX_PORT CONFIGURE_DEPENDS "${threadx_source_SOURCE_DIR}/ports/cortex_m4/gnu/src/*.S")
add_library(app_threadx STATIC ${TX_COMMON} ${TX_PORT})
target_include_directories(app_threadx PUBLIC
    ${threadx_source_SOURCE_DIR}/common/inc
    ${threadx_source_SOURCE_DIR}/ports/cortex_m4/gnu/inc
    ${APP_ROOT}/threads/include)
target_compile_definitions(app_threadx PUBLIC TX_INCLUDE_USER_DEFINE_FILE)
target_link_libraries(board app_threadx)
target_compile_definitions(board PRIVATE APP_BUILD_SERVICES=1
    APP_ENABLE_IMU=$<BOOL:${APP_ENABLE_IMU}> APP_ENABLE_CONTROL=$<BOOL:${APP_ENABLE_CONTROL}>)
target_sources(board PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../Core/Src/app_threadx_port.c
    ${APP_ROOT}/threads/src/threads.cpp
    ${APP_ROOT}/threads/src/alivethread.cpp
    ${APP_ROOT}/threads/src/imuthread.cpp
    ${APP_ROOT}/threads/src/controlthread.cpp)
