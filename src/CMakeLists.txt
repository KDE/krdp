
add_library(KRdp)

target_sources(KRdp PRIVATE
    Server.cpp
    Session.cpp
    InputHandler.cpp
    PeerContext.cpp
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
    VERSION ${VERSION}
    DEPRECATED_BASE_VERSION 0
    EXCLUDE_DEPRECATED_BEFORE_AND_AT ${EXCLUDE_DEPRECATED_BEFORE_AND_AT}
)

target_include_directories(KRdp PUBLIC ${FreeRDP_INCLUDE_DIR} ${WinPR_INCLUDE_DIR})
target_link_libraries(KRdp PUBLIC Qt${QT_MAJOR_VERSION}::Core Qt${QT_MAJOR_VERSION}::Network freerdp winpr)

if (${FreeRDP_VERSION} VERSION_LESS "3.0.0")
    target_include_directories(KRdp ${FreeRDP_Server_INCLUDE_DIR})
    target_link_libraries(KRdp freerdp-server)
endif()

# target_compile_definitions(KRdp PRIVATE -D_REENTRANT)

install(TARGETS KRdp DESTINATION ${KDE_INSTALL_LIBDIR})

ecm_qt_install_logging_categories(EXPORT KRdp DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})
