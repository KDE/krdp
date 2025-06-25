// SPDX-FileCopyrightText: 2025 David Edmundson <davidedmundson@kde.org>
//
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

#include "usersmodel.h"

UsersModel::UsersModel(KRDPServerSettings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(settings)
{
}

int UsersModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_settings->users().size() + 1;
}

QVariant UsersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_settings->users().size() +1)
        return QVariant();

    if (index.row() == 0)  {
        switch (role) {
        case DisplayRole:
            return m_currentUser.loginName();
        case SystemUserRole:
            return true;
        case SystemUserEnabledRole:
            return m_settings->systemUserEnabled();
        }
    } else {
        switch (role) {
        case DisplayRole:
            return m_settings->users().at(index.row() - 1);
        case SystemUserRole:
            return false;
        }
    }

    return QVariant();
}

bool UsersModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.row() != 0) {
        Q_ASSERT(false);
        return false;
    }
    if (role != SystemUserEnabledRole) {
        Q_ASSERT(false);
        return false;
    }
    m_settings->setSystemUserEnabled(value.toBool());
    Q_EMIT dataChanged(index, index, {role});
    return true;
}

QHash<int, QByteArray> UsersModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[DisplayRole] = "userName";
    roles[SystemUserRole] = "systemUser";
    roles[SystemUserEnabledRole] = "systemUserEnabled";
    return roles;
}

QStringList UsersModel::users() const
{
    return m_settings->users();
}

void UsersModel::setUsers(const QStringList &users)
{
    beginResetModel();
    m_settings->setUsers(users);
    endResetModel();
}
