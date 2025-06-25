# SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
#
# SPDX-License-Identifier: BSD-2-Clause

add_definitions(-DTRANSLATION_DOMAIN=\"kcm_krdpserver\")

kcmutils_add_qml_kcm(kcm_krdpserver)

kcmutils_generate_module_data(kcm_krdpserver
    MODULE_DATA_HEADER krdpserverdata.h
    MODULE_DATA_CLASS_NAME KRDPServerData
    SETTINGS_HEADERS krdpserversettings.h
    SETTINGS_CLASSES KRDPServerSettings
)

kconfig_target_kcfg_file(kcm_krdpserver
    FILE krdpserversettings.kcfg
    CLASS_NAME KRDPServerSettings
    MUTATORS
    DEFAULT_VALUE_GETTERS
    GENERATE_PROPERTIES
    PARENT_IN_CONSTRUCTOR
    NOTIFIERS
    GENERATE_MOC
)

ecm_qt_declare_logging_category(kcm_krdpserver
    HEADER krdp_logging.h
    IDENTIFIER KRDP
    CATEGORY_NAME org.kde.krdp
    DESCRIPTION "KRdp"
    EXPORT kcm_krdpserver
    DEFAULT_SEVERITY Debug
)

target_sources(kcm_krdpserver PRIVATE
   kcmkrdpserver.cpp
   kcmkrdpserver.h
   usersmodel.cpp
   usersmodel.h
)

qt_add_dbus_interface(kcm_krdpserver_dbus_srcs org.freedesktop.impl.portal.PermissionStore.xml org.freedesktop.impl.portal.PermissionStore)

target_sources(kcm_krdpserver PRIVATE ${kcm_krdpserver_dbus_srcs})

ecm_qt_declare_logging_category(kcm_krdpserver
    HEADER krdpkcm_logging.h
    IDENTIFIER KRDPKCM
    CATEGORY_NAME org.kde.krdp.kcm
    DESCRIPTION "KRDPKCM"
    EXPORT kcm_krdpserver
    DEFAULT_SEVERITY Debug
)

target_link_libraries(kcm_krdpserver PRIVATE
    Qt::Quick
    Qt::DBus
    qt6keychain
    KF6::CoreAddons
    KF6::KCMUtils
    KF6::KCMUtilsQuick
    KF6::I18n
    K::KPipeWireRecord
)

ecm_qt_install_logging_categories(EXPORT kcm_krdpserver DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})
