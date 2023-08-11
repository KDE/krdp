
add_executable(krdpserver)

target_sources(krdpserver PRIVATE main.cpp)

target_link_libraries(krdpserver Qt${QT_MAJOR_VERSION}::Gui KRdp)

install(TARGETS krdpserver DESTINATION ${KDE_INSTALL_BINDIR})

configure_file(org.kde.krdp.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.krdp.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.krdp.desktop DESTINATION ${KDE_INSTALL_APPDIR})
