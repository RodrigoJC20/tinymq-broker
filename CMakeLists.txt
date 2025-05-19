cmake_minimum_required(VERSION 3.10)
project(tinymq VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system thread)
find_package(PostgreSQL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED libpqxx)

include_directories(${Boost_INCLUDE_DIRS} ${PostgreSQL_INCLUDE_DIRS} ${PQXX_INCLUDE_DIRS})
link_directories(${PQXX_LIBRARY_DIRS})
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(tinymq_broker ${SOURCES})

target_link_libraries(tinymq_broker PRIVATE 
    ${Boost_LIBRARIES} 
    ${PostgreSQL_LIBRARIES}
    ${PQXX_LDFLAGS}
)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/db)
configure_file(${CMAKE_SOURCE_DIR}/db/schema.sql ${CMAKE_BINARY_DIR}/db/schema.sql COPYONLY)

install(TARGETS tinymq_broker DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/db/schema.sql DESTINATION share/tinymq)
