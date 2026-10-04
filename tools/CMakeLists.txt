# SSTable Dump
add_executable(sstable_dump sstable_dump.cpp)
target_link_libraries(sstable_dump PRIVATE enigmadb_lib)

# Manifest Dump
add_executable(manifestdump manifest_dump.cpp)
target_link_libraries(manifestdump PRIVATE enigmadb_lib)
