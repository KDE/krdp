# SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
# SPDX-License-Identifier: BSD-2-Clause

add_library(KRdp)

if (BUILD_PLASMA_SESSION)
    if (Qt6_VERSION VERSION_GREATER_EQUAL "6.8.0")
        set(private_code_option  "PRIVATE_CODE")
    endif()
    qt6_generate_wayland_protocol_client_sources(KRdp FILES
        ${PLASMA_WAYLAND_PROTOCOLS_DIR}/zkde-screencast-unstable-v1.xml
        ${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml
        ${Wayland_DATADIR}/wayland.xml
        ${private_code_option}
    )
    target_sources(KRdp PRIVATE screencasting.cpp PlasmaScreencastV1Session.cpp)
endif()

target_sources(KRdp PRIVATE
    AbstractSession.cpp
    Clipboard.cpp
    Clipboard.h
    RdpConnection.cpp
    Server.cpp
    Server.h
    InputHandler.cpp
    InputHandler.h
    PeerContext.cpp
    PeerContext_p.h
    PortalSession.cpp
    PortalSession.h
    VideoStream.cpp
    VideoStream.h
    Cursor.cpp
    Cursor.h
    NetworkDetection.cpp
    NetworkDetection.h
)

ecm_qt_declare_logging_category(KRdp
    HEADER krdp_logging.h
    IDENTIFIER KRDP
    CATEGORY_NAME org.kde.krdp
    DESCRIPTION "KRdp"
    EXPORT KRdp
    DEFAULT_SEVERITY Debug
)

ecm_generate_export_header(KRdp
    BASE_NAME KRdp
    VERSION ${CMAKE_PROJECT_VERSION}
    DEPRECATED_BASE_VERSION 0
    EXCLUDE_DEPRECATED_BEFORE_AND_AT ${EXCLUDE_DEPRECATED_BEFORE_AND_AT}
)

qt6_add_dbus_interface(_dbus_sources xdp_dbus_remotedesktop_interface.xml xdp_dbus_remotedesktop_interface)
qt6_add_dbus_interface(_dbus_sources xdp_dbus_screencast_interface.xml xdp_dbus_screencast_interface)
target_sources(KRdp PRIVATE ${_dbus_sources})

target_include_directories(KRdp PUBLIC ${FreeRDP_INCLUDE_DIR} ${WinPR_INCLUDE_DIR} ${FreeRDP_Server_INCLUDE_DIR})

target_link_libraries(KRdp PUBLIC
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::DBus
    Qt::WaylandClient
    Qt::GuiPrivate

    freerdp
    winpr
    freerdp-server
    KF6::ConfigCore
    K::KPipeWire
    K::KPipeWireRecord
    XKB::XKB
)

set_target_properties(KRdp PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS KRdp DESTINATION ${KDE_INSTALL_LIBDIR})

ecm_qt_install_logging_categories(EXPORT KRdp DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})

add_subdirectory(kcm)
