# Minimalni RTS8822 simulator. Pun simulator - virtuelni motor, home senzor,
# lampe, CCD sa test-metom, FailureInjector - dolazi u G2710-3.
add_library(g2710_sim STATIC SimTransport.cpp)
add_library(G2710::Sim ALIAS g2710_sim)
target_include_directories(g2710_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(g2710_sim PUBLIC G2710::Core)
