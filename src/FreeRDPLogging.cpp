/*
 *  SPDX-FileCopyrightText: 2026 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 */

#include <QMessageLogger>

#include <winpr/wlog.h>

#include "freerdp_logging.h"
#include "krdp_logging.h"

#include "FreeRDPLogging.h"

namespace
{

BOOL logWLogMessage(const wLogMessage *msg)
{
    QMessageLogger logger(msg->FileName, static_cast<int>(msg->LineNumber), msg->FunctionName, FREERDP().categoryName());

    switch (msg->Level) {
    case WLOG_TRACE:
        if (FREERDP().isDebugEnabled())
            logger.debug("%s", msg->TextString);
        break;
    case WLOG_DEBUG:
    case WLOG_INFO:
        // DEBUG is grouped with INFO as trace is very noisy, and
        // grouping it with trace instead drowns out useful info
        if (FREERDP().isInfoEnabled())
            logger.info("%s", msg->TextString);
        break;
    case WLOG_WARN:
        if (FREERDP().isWarningEnabled())
            logger.warning("%s", msg->TextString);
        break;
    case WLOG_ERROR:
        if (FREERDP().isCriticalEnabled())
            logger.critical("%s", msg->TextString);
        break;
    case WLOG_FATAL:
        logger.fatal("%s", msg->TextString);
        break;
    default:
        break;
    }

    return TRUE;
}

}

namespace KRdp
{

void installFreeRDPLogging()
{
    wLog *root = WLog_GetRoot();
    if (!root) {
        qCWarning(KRDP) << "Failed to get FreeRDP WLog root";
        return;
    }

    if (!WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK)) {
        qCWarning(KRDP) << "Failed to set FreeRDP log appender type";
        return;
    }

    wLogCallbacks callbacks = {};
    callbacks.message = logWLogMessage;

    if (!WLog_ConfigureAppender(WLog_GetLogAppender(root), "callbacks", &callbacks)) {
        qCWarning(KRDP) << "Failed to configure FreeRDP log callbacks";
        return;
    }

    if (!WLog_SetLogLevel(root,
                          FREERDP().isDebugEnabled()          ? WLOG_TRACE
                              : FREERDP().isInfoEnabled()     ? WLOG_DEBUG
                              : FREERDP().isWarningEnabled()  ? WLOG_WARN
                              : FREERDP().isCriticalEnabled() ? WLOG_ERROR
                                                              : WLOG_FATAL)) {
        qCWarning(KRDP) << "Failed to set FreeRDP log level";
        return;
    }

    qCDebug(KRDP) << "Forwarding FreeRDP WLog messages to QLoggingCategory com.freerdp";
}

}
