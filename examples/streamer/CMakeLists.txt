# SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
# SPDX-License-Identifier: BSD-2-Clause

add_executable(krdpstreamer)

target_sources(krdpstreamer PRIVATE main.cpp)

target_link_libraries(krdpstreamer Qt${QT_MAJOR_VERSION}::Gui KRdp)


