set(PROJECT_VERSION "5.27.80")
set(PROJECT_VERSION_MAJOR 5)

cmake_minimum_required(VERSION 3.16)

set(QT_MIN_VERSION "6.4.0")
set(KF6_MIN_VERSION "5.240.0")
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

option(BUILD_EXAMPLES "Build example applications" ON)
option(BUILD_PLASMA_SESSION "Build support for Plasma Screencasting extensions" ON)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED Core Quick Gui Network DBus WaylandClient Qml)

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    Config
    CoreAddons
    KCMUtils
    I18n
    DBusAddons
    JobWidgets
)

find_package(FreeRDP 2.10 REQUIRED)
find_package(WinPR REQUIRED)
find_package(FreeRDP-Server REQUIRED)
find_package(KPipeWire 5.27.80 REQUIRED)
find_package(XKB REQUIRED)
find_package(PlasmaWaylandProtocols REQUIRED)

set(EXCLUDE_DEPRECATED_BEFORE_AND_AT 0 CACHE STRING "Control the range of deprecated API excluded from the build [default=0].")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

add_subdirectory(src)

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

kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)
