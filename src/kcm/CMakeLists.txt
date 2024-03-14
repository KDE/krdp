# SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
#
# SPDX-License-Identifier: BSD-2-Clause

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_definitions(-DTRANSLATION_DOMAIN=\"kcm_krdp\")

kcmutils_add_qml_kcm(kcm_krdp)
target_sources(kcm_krdp PRIVATE
   kcm_krdp.cpp
   kcm_krdp.h
)
target_link_libraries(kcm_krdp PRIVATE
    Qt::Quick
    Qt::DBus
    qt6keychain
    KF6::CoreAddons
    KF6::KCMUtils
    KF6::I18n
    KF6::AuthCore
)
