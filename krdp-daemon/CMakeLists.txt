# SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
# SPDX-License-Identifier: BSD-2-Clause

add_definitions(-DTRANSLATION_DOMAIN=\"krdp-daemon\")

add_executable(krdp-daemon)

ecm_qt_declare_logging_category(krdp-daemon
    HEADER krdpd_logging.h
    IDENTIFIER KRDPD
    CATEGORY_NAME org.kde.krdp.daemon
    DESCRIPTION "KRdp daemon"
)

target_sources(krdp-daemon PRIVATE
    main.cpp
    DaemonServer.cpp
    DaemonRdpConnection.cpp
    DaemonPeerContext.cpp
)

target_include_directories(krdp-daemon PRIVATE ${FreeRDP_INCLUDE_DIR} ${WinPR_INCLUDE_DIR} ${FreeRDP_Server_INCLUDE_DIR})
kconfig_target_kcfg_file(krdp-daemon FILE krdpserversettings.kcfg CLASS_NAME ServerConfig SINGLETON)

target_include_directories(krdp-daemon PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(krdp-daemon PRIVATE Qt6::Gui Qt6::Network KF6::CoreAddons KF6::ConfigGui KF6::DBusAddons KF6::Crash qt6keychain KF6::StatusNotifierItem KF6::I18n

    freerdp
    winpr
    freerdp-server
    ${PAM_LIBRARIES}
)

if (BUILD_PLASMA_SESSION)
    target_compile_definitions(krdp-daemon PRIVATE -DWITH_PLASMA_SESSION)
endif()

#configure_file(org.kde.krdpserver.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.krdpserver.desktop @ONLY)
#configure_file(app-org.kde.krdpserver.service.in ${CMAKE_CURRENT_BINARY_DIR}/app-org.kde.krdpserver.service @ONLY)

#install(TARGETS krdpserver DESTINATION ${KDE_INSTALL_BINDIR})
#install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.krdpserver.desktop DESTINATION ${KDE_INSTALL_APPDIR})
#install(FILES ${CMAKE_CURRENT_BINARY_DIR}/app-org.kde.krdpserver.service DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
#install(FILES 00-krdp.preset DESTINATION ${KDE_INSTALL_SYSTEMDUNITDIR}/user-preset)
