
add_executable(krdpserver)

target_sources(krdpserver PRIVATE main.cpp)

target_link_libraries(krdpserver Qt${QT_MAJOR_VERSION}::Gui KRdp)

install(TARGETS krdpserver DESTINATION ${KDE_INSTALL_BINDIR})
