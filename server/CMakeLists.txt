
add_executable(krdpserver)

target_sources(krdpserver PRIVATE main.cpp)

kconfig_target_kcfg_file(krdpserver FILE krdpserverrc.kcfg CLASS_NAME ServerConfig SINGLETON)

target_link_libraries(krdpserver PRIVATE Qt6::Gui KF6::ConfigGui KF6::DBusAddons KRdp)

if (BUILD_PLASMA_SESSION)
    target_compile_definitions(krdpserver PRIVATE -DWITH_PLASMA_SESSION)
endif()

configure_file(org.kde.krdp.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.krdp.desktop @ONLY)
configure_file(org.kde.krdp.service.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.krdp.service @ONLY)

install(TARGETS krdpserver DESTINATION ${KDE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.krdp.desktop DESTINATION ${KDE_INSTALL_APPDIR})
