
add_executable(krdpserver)

target_sources(krdpserver PRIVATE main.cpp SessionController.cpp)

kconfig_target_kcfg_file(krdpserver FILE krdpserverrc.kcfg CLASS_NAME ServerConfig SINGLETON)

target_link_libraries(krdpserver PRIVATE Qt6::Gui KF6::ConfigGui KF6::DBusAddons KRdp)

if (BUILD_PLASMA_SESSION)
    target_compile_definitions(krdpserver PRIVATE -DWITH_PLASMA_SESSION)
endif()

configure_file(plasma-krdp_server.service.in ${CMAKE_CURRENT_BINARY_DIR}/plasma-krdp_server.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plasma-krdp_server.service DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
