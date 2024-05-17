#! /usr/bin/env bash
# SPDX-FileCopyrightText: 2024 Akseli Lahtinen <akselmo@akselmo.dev>
# SPDX-License-Identifier: CC0-1.0

$EXTRACTRC `find . -name \*.kcfg` >> rc.cpp
$XGETTEXT `find . -name "*.cpp" -o $podir/krdpserver.pot
