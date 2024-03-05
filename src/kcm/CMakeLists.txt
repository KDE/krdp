# SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
#
# SPDX-License-Identifier: BSD-2-Clause

add_definitions(-DTRANSLATION_DOMAIN=\"kcmkrdpserver\")

kcmutils_add_qml_kcm(kcmkrdpserver)

kcmutils_generate_module_data(kcmkrdpserver
    MODULE_DATA_HEADER krdpserverdata.h
    MODULE_DATA_CLASS_NAME KRDPServerData
    SETTINGS_HEADERS krdpserversettings.h
    SETTINGS_CLASSES KRDPServerSettings
)
kconfig_add_kcfg_files(kcmkrdpserver krdpserversettings.kcfgc GENERATE_MOC)

target_sources(kcmkrdpserver PRIVATE
   kcmkrdpserver.cpp
   kcmkrdpserver.h
)

ecm_qt_declare_logging_category(kcmkrdpserver
    HEADER krdpkcm_logging.h
    IDENTIFIER KRDPKCM
    CATEGORY_NAME org.kde.krdp.kcm
    DESCRIPTION "KRDPKCM"
    EXPORT kcmkrdpserver
    DEFAULT_SEVERITY Debug
)

target_link_libraries(kcmkrdpserver PRIVATE
    Qt::Quick
    Qt::DBus
    qt6keychain
    KF6::CoreAddons
    KF6::KCMUtils
    KF6::KCMUtilsQuick
    KF6::I18n
)

ecm_qt_install_logging_categories(EXPORT kcmkrdpserver DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})
