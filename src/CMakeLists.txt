
add_library(KRdp)

target_sources(KRdp PRIVATE
    Server.cpp
    Session.cpp
    InputHandler.cpp
    PeerContext.cpp
    PortalSession.cpp
    VideoStream.cpp
    Cursor.cpp
    NetworkDetection.cpp
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

if ("${FreeRDP_VERSION_MAJOR}" STREQUAL "3")
    target_compile_definitions(KRdp PRIVATE -DFREERDP3)
endif()

target_include_directories(KRdp PUBLIC ${FreeRDP_INCLUDE_DIR} ${WinPR_INCLUDE_DIR} ${FreeRDP_Server_INCLUDE_DIR})

target_link_libraries(KRdp PUBLIC
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::DBus
    freerdp
    winpr
    freerdp-server
    K::KPipeWire
    K::KPipeWireRecord
    XKB::XKB
)

install(TARGETS KRdp DESTINATION ${KDE_INSTALL_LIBDIR})

ecm_qt_install_logging_categories(EXPORT KRdp DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})
