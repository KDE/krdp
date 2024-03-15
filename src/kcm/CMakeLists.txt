# SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
#
# SPDX-License-Identifier: BSD-2-Clause

add_definitions(-DTRANSLATION_DOMAIN=\"kcmkrdpserver\")

kcmutils_add_qml_kcm(kcmkrdpserver)
target_sources(kcmkrdpserver PRIVATE
   kcmkrdpserver.cpp
   kcmkrdpserver.h
)

target_link_libraries(kcmkrdpserver PRIVATE
    Qt::Quick
    Qt::DBus
    qt6keychain
    KF6::CoreAddons
    KF6::KCMUtils
    KF6::I18n
)
