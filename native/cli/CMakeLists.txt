add_executable(g2710ctl main.cpp ImageOutput.cpp Qualification.cpp)
set_target_properties(g2710ctl PROPERTIES OUTPUT_NAME "g2710ctl")
# dynamic_cast na UsbScanTransport u probe komandi trazi RTTI (podrazumevano ukljucen).
target_link_libraries(g2710ctl PRIVATE G2710::Core G2710::Sim)
