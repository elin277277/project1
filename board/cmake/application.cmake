# Hand-maintained layer integration; CubeMX does not own this file.
set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_language(CXX)
option(APP_BUILD_MODULES "Compile device modules and algorithms" OFF)
option(APP_BUILD_SERVICES "Run ThreadX services" OFF)
option(APP_ENABLE_IMU "Start IMU sampling service" ON)
option(APP_ENABLE_CONTROL "Start motor control service (hardware must be confirmed)" OFF)

target_sources(board PRIVATE
    ${APP_ROOT}/bsp/src/bsp.cpp
    ${APP_ROOT}/bsp/src/bsp_can.cpp
    ${APP_ROOT}/bsp/src/bsp_usart.cpp
    ${APP_ROOT}/bsp/src/bsp_pwm.cpp)
target_include_directories(board PRIVATE ${APP_ROOT}/bsp/include)
if(APP_BUILD_MODULES OR APP_BUILD_SERVICES)
    target_sources(board PRIVATE
        ${APP_ROOT}/libs/src/pid.cpp ${APP_ROOT}/libs/src/math.cpp ${APP_ROOT}/libs/src/crc.cpp
        ${APP_ROOT}/modules/src/BMI088.cpp ${APP_ROOT}/modules/src/LED.cpp
        ${APP_ROOT}/modules/src/M2006.cpp ${APP_ROOT}/modules/src/DJIMotorHandler.cpp)
    target_include_directories(board PRIVATE ${APP_ROOT}/libs/include ${APP_ROOT}/modules/include)
endif()
if(APP_BUILD_SERVICES)
    include(${CMAKE_CURRENT_LIST_DIR}/services.cmake)
endif()
