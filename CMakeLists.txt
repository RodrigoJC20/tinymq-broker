cmake_minimum_required(VERSION 3.10)
project(tinymq VERSION 0.1.0)

# Enable compile commands export
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost (only need system and thread components for ASIO)
find_package(Boost REQUIRED COMPONENTS system thread)

# Find PostgreSQL development libraries
find_package(PostgreSQL REQUIRED)

# Find libpqxx (C++ interface to PostgreSQL) - might need adjustment based on your system setup
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED libpqxx)

# Include directories
include_directories(${Boost_INCLUDE_DIRS} ${PostgreSQL_INCLUDE_DIRS} ${PQXX_INCLUDE_DIRS})

# Add all source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create executable
add_executable(tinymq_broker ${SOURCES})

# Link libraries
target_link_libraries(tinymq_broker PRIVATE ${Boost_LIBRARIES} ${PostgreSQL_LIBRARIES} ${PQXX_LIBRARIES})

# Create directory for database files
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/db)

# Copy database schema file to build directory
configure_file(${CMAKE_SOURCE_DIR}/db/schema.sql ${CMAKE_BINARY_DIR}/db/schema.sql COPYONLY)

# Install targets
install(TARGETS tinymq_broker DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/db/schema.sql DESTINATION share/tinymq) 
