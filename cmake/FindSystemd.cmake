# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2021 Henri Chain <henri.chain@enioka.com>
# SPDX-FileCopyrightText: 2021 Harald Sitter <sitter@kde.org>

# Try to find systemd on a linux system
# This will define the following variables:
#
# ``Systemd_FOUND``
#     True if systemd is available
# ``Systemd_VERSION``
#     The version of systemd

find_package(PkgConfig QUIET)
pkg_check_modules(Systemd QUIET IMPORTED_TARGET GLOBAL libsystemd)

if(TARGET PkgConfig::Systemd)
    add_library(Systemd::systemd ALIAS PkgConfig::Systemd)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Systemd
    REQUIRED_VARS
        Systemd_FOUND
    VERSION_VAR
        Systemd_VERSION
)

mark_as_advanced(Systemd_VERSION)
