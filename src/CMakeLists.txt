file(GLOB_RECURSE LIB_SOURCES "*.cpp")
list(FILTER LIB_SOURCES EXCLUDE REGEX "main\\.cpp$")

add_library(enigmadb_lib ${LIB_SOURCES})
target_include_directories(enigmadb_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)

# target_include_directories(enigmadb_lib PUBLIC
#   $<TARGET_PROPERTY:gtest,INTERFACE_INCLUDE_DIRECTORIES>)

add_executable(enigmadb main.cpp)
target_link_libraries(enigmadb PRIVATE enigmadb_lib)
