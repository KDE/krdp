
add_library(KRdp)

target_sources(KRdp PRIVATE
    Server.cpp
)

ecm_qt_declare_logging_category(KRdp
    HEADER krdp_logging.h
    IDENTIFIER KRDP
    CATEGORY_NAME org.kde.krdp
    DESCRIPTION "KRdp"
    EXPORT KRdp
)

ecm_generate_export_header(KRdp
    BASE_NAME KRdp
    VERSION ${VERSION}
    DEPRECATED_BASE_VERSION 0
    EXCLUDE_DEPRECATED_BEFORE_AND_AT ${EXCLUDE_DEPRECATED_BEFORE_AND_AT}
)

target_link_libraries(KRdp PUBLIC Qt${QT_MAJOR_VERSION}::Core Qt${QT_MAJOR_VERSION}::Network)

install(TARGETS KRdp DESTINATION ${KDE_INSTALL_LIBDIR})

ecm_qt_install_logging_categories(EXPORT KRdp DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})
