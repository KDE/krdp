# SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
#
# SPDX-License-Identifier: BSD-2-Clause

add_definitions(-DTRANSLATION_DOMAIN=\"krdp-server-service\")

add_definitions(-DQT_NO_URL_CAST_FROM_STRING)

set(kded_krdpservice krdpservice.cpp krdpservice.h)

add_library(krdpservice MODULE ${kded_krdpservice})

target_link_libraries(krdpservice
    Qt::DBus
    Qt::Widgets
    KF6::CoreAddons
    KF6::DBusAddons
    KF6::I18n
)

install(TARGETS krdpservice DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/kded)
