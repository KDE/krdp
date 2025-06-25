# SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
# SPDX-License-Identifier: BSD-2-Clause

set(PROJECT_VERSION "6.4.80")

cmake_minimum_required(VERSION 3.16)

set(QT_MIN_VERSION "6.8.0")
set(KF6_MIN_VERSION "6.14.0")
set(KDE_COMPILERSETTINGS_LEVEL "5.85")

project(KRdp VERSION "${PROJECT_VERSION}")

include(FeatureSummary)
find_package(ECM ${KF6_MIN_VERSION} NO_MODULE)
set_package_properties(ECM PROPERTIES TYPE REQUIRED DESCRIPTION "Extra CMake Modules." URL "https://commits.kde.org/extra-cmake-modules")
feature_summary(WHAT REQUIRED_PACKAGES_NOT_FOUND FATAL_ON_MISSING_REQUIRED_PACKAGES)

set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake )

include(KDEInstallDirs)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDECMakeSettings)
include(KDEGitCommitHooks)

include(KDEClangFormat)
include(ECMGenerateExportHeader)
include(ECMAddTests)
include(ECMSetupVersion)
include(ECMQtDeclareLoggingCategory)
include(ECMEnableSanitizers)
include(ECMDeprecationSettings)
include(ECMQmlModule)

option(BUILD_EXAMPLES "Build example applications" ON)
option(BUILD_PLASMA_SESSION "Build support for Plasma Screencasting extensions" ON)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED Core Quick Gui Network DBus WaylandClient Qml)

if (Qt6Gui_VERSION VERSION_GREATER_EQUAL "6.10.0")
    find_package(Qt6GuiPrivate ${REQUIRED_QT_VERSION} REQUIRED NO_MODULE)
endif()

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS Config DBusAddons KCMUtils I18n CoreAddons StatusNotifierItem Crash GuiAddons)

find_package(FreeRDP 3.1 REQUIRED)
find_package(WinPR 3.1 REQUIRED)
find_package(FreeRDP-Server 3.1 REQUIRED)
find_package(KPipeWire 5.27.80 REQUIRED)
find_package(XKB REQUIRED)

find_package(Qt6Keychain CONFIG)
set_package_properties(Qt6Keychain PROPERTIES
                                   DESCRIPTION "Provides support for secure credentials storage"
                                   URL "https://github.com/frankosterfeld/qtkeychain"
				   TYPE REQUIRED)

if(BUILD_PLASMA_SESSION)
    find_package(PlasmaWaylandProtocols REQUIRED)
endif()

find_package(PAM REQUIRED)
set_package_properties(PAM PROPERTIES DESCRIPTION "PAM Libraries"
                       URL "https://www.kernel.org/pub/linux/libs/pam/"
                       TYPE REQUIRED
                       PURPOSE "Required for authentication"
                      )

set(EXCLUDE_DEPRECATED_BEFORE_AND_AT 0 CACHE STRING "Control the range of deprecated API excluded from the build [default=0].")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)


ecm_set_disabled_deprecation_versions(QT 6.7.0
    KF 6.12.0
)

add_subdirectory(src)
add_subdirectory(server)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_TESTING)
    add_subdirectory(autotests)
endif()

ecm_setup_version(
    PROJECT
    VARIABLE_PREFIX KRdp
    VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/krdp_version.h"
    PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/KRdpConfigVersion.cmake"
    SOVERSION 1
)

configure_package_config_file(
    "KRdpConfig.cmake.in"
    "KRdpConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/KRdp
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/KRdpConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/KRdpConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/KRdp
    COMPONENT Devel
)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES *.cpp *.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)

ki18n_install(po)
