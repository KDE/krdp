// SPDX-FileCopyrightText: 2026 Tobias Ozór <tobiasozor@outlook.com>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#pragma once

#include <KCModuleData>
#include <QObject>

class KRDPServerSettings;

class KRDPServerData : public KCModuleData
{
    Q_OBJECT

public:
    explicit KRDPServerData(QObject *parent = nullptr);
    KRDPServerSettings *settings() const;

    bool isDefaults() const override;

private:
    KRDPServerSettings *m_settings;
};
