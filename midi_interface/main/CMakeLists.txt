idf_component_register(SRCS "main.cpp" "usb_midi_host.cpp" "../music/music.cpp"
    INCLUDE_DIRS "." "../music" "../../shared"
    REQUIRES usb driver esp_timer)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
