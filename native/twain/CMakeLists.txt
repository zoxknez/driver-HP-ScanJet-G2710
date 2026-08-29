# TWAIN Data Source se gradi za obe arhitekture. Ne zavisi od DSM-a pri
# kompajliranju; DSM ga ucitava i poziva samo kroz izvezeni DS_Entry.
add_library(g2710_twain SHARED
    TwainDataSource.cpp
    G2710Twain.def
)

set_target_properties(g2710_twain PROPERTIES
    OUTPUT_NAME "G2710.Twain"
    PREFIX ""
)

target_include_directories(g2710_twain PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/twain
)
target_link_libraries(g2710_twain PRIVATE G2710::Core)

g2710_add_version_resource(g2710_twain "HP ScanJet G2710 TWAIN Data Source" "G2710.Twain.ds" VFT_DLL)

target_compile_definitions(g2710_twain PRIVATE
    G2710_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    G2710_VERSION_MINOR=${PROJECT_VERSION_MINOR})
