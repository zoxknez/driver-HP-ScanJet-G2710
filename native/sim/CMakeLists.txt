# Simulator RTS8822 uredjaja.
#
# Modelira ono sto engine mora da izdrzi: motor sa mehanickim granicama,
# lampu koja se zagreva, CCD koji NAMERNO gresi, i otkaze transporta koji se
# zakazuju eksplicitno da bi testovi bili ponovljivi.
add_library(g2710_sim STATIC
    FailureInjector.cpp
    ScanEngine.cpp
    SimTransport.cpp
    TestTarget.cpp
    VirtualCcd.cpp
    VirtualLamp.cpp
    VirtualMotor.cpp
)
add_library(G2710::Sim ALIAS g2710_sim)
target_include_directories(g2710_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(g2710_sim PUBLIC G2710::Core)
