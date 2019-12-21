cmake_minimum_required(VERSION 3.0.0)
project(pulsation VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)

aux_source_directory(. SRCS)
add_executable(pulsation ${SRCS})

find_package (Threads)
find_package(ZLIB)
find_package(Boost COMPONENTS system filesystem REQUIRED)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

target_link_libraries(pulsation
  ${CMAKE_THREAD_LIBS_INIT}
  ${ZLIB_LIBRARIES}
  ${Boost_FILESYSTEM_LIBRARY}
  ${Boost_SYSTEM_LIBRARY}
)
