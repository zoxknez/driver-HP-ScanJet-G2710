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

