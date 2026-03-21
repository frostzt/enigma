file(GLOB_RECURSE LIB_SOURCES "*.cpp")

add_library(enigmadb_lib ${LIB_SOURCES})

target_include_directories(enigmadb_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
