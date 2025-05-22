# SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
# SPDX-License-Identifier: BSD-2-Clause

add_definitions(-DTRANSLATION_DOMAIN=\"krdpserver\")

add_executable(krdpserver)

target_sources(krdpserver PRIVATE main.cpp SessionController.cpp)

kconfig_target_kcfg_file(krdpserver FILE krdpserversettings.kcfg CLASS_NAME ServerConfig SINGLETON)

target_include_directories(krdpserver PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(krdpserver PRIVATE Qt6::Gui KF6::CoreAddons KF6::ConfigGui KF6::DBusAddons KF6::Crash KRdp qt6keychain KF6::StatusNotifierItem KF6::I18n)

if (BUILD_PLASMA_SESSION)
    target_compile_definitions(krdpserver PRIVATE -DWITH_PLASMA_SESSION)
endif()

configure_file(org.kde.krdpserver.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.krdpserver.desktop @ONLY)
configure_file(app-org.kde.krdpserver.service.in ${CMAKE_CURRENT_BINARY_DIR}/app-org.kde.krdpserver.service @ONLY)

install(TARGETS krdpserver DESTINATION ${KDE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.krdpserver.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/app-org.kde.krdpserver.service DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
