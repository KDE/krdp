// SPDX-FileCopyrightText: 2023 Arjen Hiemstra <ahiemstra@heimr.nl>
//
// This file is roughly based on grd-session-rdp.c from gnome-remote-desktop,
// which is:
//
// SPDX-FileCopyrightText: 2020-2023 Pascal Nowack
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DaemonRdpConnection.h"

#include <filesystem>

#include <fcntl.h>

#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QThread>

#include <freerdp/channels/wtsvc.h>
#include <freerdp/freerdp.h>
#include <freerdp/redirection.h>
#include <freerdp/server/cliprdr.h>

#include <freerdp/channels/drdynvc.h>

#include "DaemonPeerContext_p.h"
#include "DaemonServer.h"

#include <KUser>

#include "krdpd_logging.h"

namespace fs = std::filesystem;

namespace KRdp
{

/**
 * Create the "sam" file used by FreeRDP for reading username and password
 * information. It hashes the password in the appropriate format and writes that
 * along with the username to the provided temporary file.
 */
static bool createSamFile(QTemporaryFile &file, const QList<User> &users)
{
    auto runtimePath = fs::path(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation).toStdString());

    auto path = runtimePath / "krdp";
    fs::create_directories(path);

    file.setFileTemplate(QString::fromStdString(path / "rdp-sam-XXXXXX"));
    if (!file.open()) {
        qCWarning(KRDPD) << "Could not open SAM file";
        return false;
    }

    QString data;

    for (const auto &user : users) {
        auto username = user.name;
        auto password = user.password.toUtf8();

        std::array<uint8_t, 16> hash;
        NTOWFv1A((LPSTR)password.data(), password.size(), hash.data());

        auto entry = QStringLiteral("%1:::").arg(username);
        for (int i = 0; i < 16; ++i) {
            entry.append(QStringLiteral("%1").arg(hash[i], 2, 16, QLatin1Char('0')));
        }
        entry.append(QStringLiteral(":::\n"));
        data.append(entry);
    }

    file.write(data.toUtf8());
    file.close();

    return true;
}

#include <security/pam_appl.h>

typedef struct {
    QByteArray user;
    QByteArray password;
} RDPConnectionAuthData;

typedef struct {
    pam_handle_t *handle;
    struct pam_conv pamc;
    RDPConnectionAuthData appdata;
} RdpConnectionPamHandle;

static int pam_conv(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata_ptr)
{
    int pam_status = PAM_CONV_ERR;
    RDPConnectionAuthData *appdata = NULL;
    struct pam_response *response = NULL;
    WINPR_ASSERT(num_msg >= 0);
    appdata = (RDPConnectionAuthData *)appdata_ptr;
    WINPR_ASSERT(appdata);

    if (!(response = (struct pam_response *)calloc((size_t)num_msg, sizeof(struct pam_response))))
        return PAM_BUF_ERR;

    for (int index = 0; index < num_msg; index++) {
        switch (msg[index]->msg_style) {
        case PAM_PROMPT_ECHO_ON:
            response[index].resp = _strdup(appdata->user.constData());

            if (!response[index].resp)
                goto out_fail;

            response[index].resp_retcode = PAM_SUCCESS;
            break;

        case PAM_PROMPT_ECHO_OFF:
            response[index].resp = _strdup(appdata->password.constData());

            if (!response[index].resp)
                goto out_fail;

            response[index].resp_retcode = PAM_SUCCESS;
            break;

        default:
            pam_status = PAM_CONV_ERR;
            goto out_fail;
        }
    }

    *resp = response;
    return PAM_SUCCESS;
out_fail:

    for (int index = 0; index < num_msg; ++index) {
        if (response[index].resp) {
            memset(response[index].resp, 0, strlen(response[index].resp));
            free(response[index].resp);
        }
    }

    memset(response, 0, sizeof(struct pam_response) * (size_t)num_msg);
    free(response);
    *resp = NULL;
    return pam_status;
}

static int pamAuthenticate(const QString &user, const QString &password)
{
    int pam_status = 0;
    RdpConnectionPamHandle info = {0};

    info.appdata.user = user.toLatin1();
    info.appdata.password = password.toLatin1();
    info.pamc.conv = &pam_conv;
    info.pamc.appdata_ptr = &info.appdata;

    pam_status = pam_start("login", 0, &info.pamc, &info.handle);

    if (pam_status != PAM_SUCCESS) {
        qWarning() << "pam_start failure:" << pam_strerror(info.handle, pam_status);
        return -1;
    }

    pam_status = pam_authenticate(info.handle, 0);

    if (pam_status != PAM_SUCCESS) {
        qWarning() << "pam_authenticate failure:" << pam_strerror(info.handle, pam_status);
        return -1;
    }

    pam_status = pam_acct_mgmt(info.handle, 0);

    if (pam_status != PAM_SUCCESS) {
        qWarning() << "pam_acct_mgmt failure:" << pam_strerror(info.handle, pam_status);
        return -1;
    }

    return 1;
}

/**
 * FreeRDP callback for the capabilities event.
 */
BOOL peerCapabilities(freerdp_peer *peer)
{
    auto context = contextForPeer(peer);
    if (context->connection->onCapabilities()) {
        return TRUE;
    }

    return FALSE;
}

/**
 * FreeRDP callback for the post connect event.
 */
BOOL peerPostConnect(freerdp_peer *peer)
{
    auto context = contextForPeer(peer);
    if (context->connection->onPostConnect()) {
        return TRUE;
    }

    return FALSE;
}

/**
 * FreeRDP callback for the activate event.
 */
BOOL peerActivate(freerdp_peer *peer)
{
    auto context = reinterpret_cast<DaemonPeerContext *>(peer->context);
    if (context->connection->onActivate()) {
        return TRUE;
    }

    return FALSE;
}

class DaemonRdpConnection::Private
{
public:
    DaemonServer *server = nullptr;

    State state = State::Initial;

    qintptr socketHandle;

    /*std::unique_ptr<DisplayControl> displayControl;*/ // TODO RM

    freerdp_peer *peer = nullptr;

    std::jthread thread;

    // Manual-reset event the run thread waits on alongside the peer transport
    // handles. Signalling it wakes the thread for teardown without any protocol
    // I/O on the peer from the calling thread.
    HANDLE stopEvent = nullptr;

    // Ask the run thread to finish, from any thread. It owns the peer and closes
    // it as it exits, so callers never touch the peer themselves.
    void requestStop()
    {
        thread.request_stop();
        if (stopEvent) {
            SetEvent(stopEvent);
        }
    }

    QTemporaryFile samFile;
};

DaemonRdpConnection::DaemonRdpConnection(DaemonServer *server, qintptr socketHandle)
    : QObject(nullptr)
    , d(std::make_unique<Private>())
{
    d->server = server;
    d->socketHandle = socketHandle;

    /*d->displayControl = std::make_unique<DisplayControl>(this);*/ // TODO RM

    QMetaObject::invokeMethod(this, &DaemonRdpConnection::initialize, Qt::QueuedConnection);
}

DaemonRdpConnection::~DaemonRdpConnection()
{
    // The run thread owns the peer transport and closes it as it exits, so just
    // wake it (stopEvent) and join before we free. Closing the peer here, while
    // the run thread still reads the same transport, races inside FreeRDP and
    // crashes on shutdown.
    if (d->thread.joinable()) {
        d->requestStop();
        d->thread.join();
    } else if (d->peer && d->state != State::Closed) {
        // The run thread was never started (peer set up but initialize failed):
        // nothing else touches the peer, so close it here before freeing.
        d->peer->Close(d->peer);
    }

    if (d->peer) {
        // freerdp_peer_free() does not free the context allocated by
        // freerdp_peer_context_new_ex().
        freerdp_peer_context_free(d->peer);
        freerdp_peer_free(d->peer);
    }

    if (d->stopEvent) {
        CloseHandle(d->stopEvent);
    }
}

DaemonRdpConnection::State DaemonRdpConnection::state() const
{
    return d->state;
}

void DaemonRdpConnection::setState(KRdp::DaemonRdpConnection::State newState)
{
    if (newState == d->state) {
        return;
    }

    d->state = newState;
    Q_EMIT stateChanged(newState);
}

void DaemonRdpConnection::close(DaemonRdpConnection::CloseReason reason)
{
    if (d->state == State::Closed) {
        return;
    }

    switch (reason) {
    case CloseReason::VideoInitFailed:
        freerdp_set_error_info(d->peer->context->rdp, ERRINFO_GRAPHICS_SUBSYSTEM_FAILED);
        break;
    case CloseReason::None:
        break;
    }

    // Hand teardown to the run thread; it owns the peer transport and closes it
    // as it exits. This is called from the main thread (SessionController) and
    // from video-encoding threads (VideoStream), so it must not drive the peer
    // directly - that would race with the run thread reading the same transport.
    d->requestStop();
}

void DaemonRdpConnection::sendRedirection(const QString &redirectionToken)
{
    const QByteArray routingToken = redirectionToken.toUtf8();
    auto *redirection = redirection_new();
    if (!redirection) {
        qCWarning(KRDPD) << "Failed to create redirection PDU";
        return;
    }

    redirection_set_byte_option(redirection, LB_LOAD_BALANCE_INFO, reinterpret_cast<const BYTE *>(routingToken.constData()), routingToken.size());
    redirection_set_flags(redirection, LB_LOAD_BALANCE_INFO);

    uint32_t incorrectFlags = 0;
    if (!redirection_settings_are_valid(redirection, &incorrectFlags)) {
        qCWarning(KRDPD) << "Invalid redirection PDU flags:" << incorrectFlags;
        redirection_free(redirection);
        return;
    }

    if (!d->peer->SendServerRedirection(d->peer, redirection)) {
        qCWarning(KRDPD) << "Failed to send server redirection";
    }
    redirection_free(redirection);
}

void DaemonRdpConnection::initialize()
{
    setState(State::Starting);

    d->peer = freerdp_peer_new(d->socketHandle);
    if (!d->peer) {
        qCWarning(KRDPD) << "Failed to create peer";
        return;
    }

    // Create an instance of our custom PeerContext extended context as context
    // rather than the plain rdpContext.
    d->peer->ContextSize = sizeof(DaemonPeerContext);
    d->peer->ContextNew = (psPeerContextNew)newPeerContext;
    d->peer->ContextFree = (psPeerContextFree)freePeerContext;

    auto result = freerdp_peer_context_new_ex(d->peer, d->server->rdpSettings());
    if (!result) {
        qCWarning(KRDPD) << "Failed to create peer context";
        return;
    }

    auto context = reinterpret_cast<DaemonPeerContext *>(d->peer->context);
    context->connection = this;

    auto settings = d->peer->context->settings;

    const bool usePamAuthentication = d->server->usePAMAuthentication();

    if (!usePamAuthentication) {
        if (!createSamFile(d->samFile, d->server->users())) {
            qFatal("Failed to create SAM database");
            return;
        }

        if (!freerdp_settings_set_string(settings, FreeRDP_NtlmSamFile, d->samFile.fileName().toUtf8().constData())) {
            qFatal("Failed to set SAM database");
            return;
        }
    }

    auto certificate = freerdp_certificate_new_from_file(d->server->tlsCertificate().string().data());
    if (!certificate) {
        qCWarning(KRDPD) << "Could not read certificate file" << d->server->tlsCertificate().string();
        return;
    }
    freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerCertificate, certificate, 1);

    auto key = freerdp_key_new_from_file(d->server->tlsCertificateKey().string().data());
    if (!key) {
        qCWarning(KRDPD) << "Could not read certificate file" << d->server->tlsCertificate().string();
        return;
    }
    freerdp_settings_set_pointer_len(settings, FreeRDP_RdpServerRsaKey, key, 1);

    freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, false);
    freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, usePamAuthentication);
    freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, !usePamAuthentication);

    freerdp_settings_set_uint32(settings, FreeRDP_OsMajorType, OSMAJORTYPE_UNIX);
    // PSEUDO_XSERVER is apparently required for things to work properly.
    freerdp_settings_set_uint32(settings, FreeRDP_OsMinorType, OSMINORTYPE_PSEUDO_XSERVER);

    freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32);

    d->peer->Capabilities = peerCapabilities;
    d->peer->Activate = peerActivate;
    d->peer->PostConnect = peerPostConnect;

    if (!d->peer->Initialize(d->peer)) {
        qCWarning(KRDPD) << "Unable to initialize peer";
        return;
    }

    qCDebug(KRDPD) << "Session setup completed, start processing...";

    // Manual-reset event the run thread waits on, so teardown can wake it
    // without protocol I/O on the peer. Created before the thread starts.
    d->stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    // Perform actual communication on a separate thread.
    d->thread = std::jthread(std::bind(&DaemonRdpConnection::run, this, std::placeholders::_1));
    pthread_setname_np(d->thread.native_handle(), "krdp_session");
}

void DaemonRdpConnection::run(std::stop_token stopToken)
{
    auto context = reinterpret_cast<DaemonPeerContext *>(d->peer->context);
    auto channelEvent = WTSVirtualChannelManagerGetEventHandle(context->virtualChannelManager);
    BYTE lastDrdynvcState = 0xFF;
    bool lastDrdynvcJoined = false;
    setState(State::Running);

    while (!stopToken.stop_requested()) {
        // events[0] = virtual channel manager, events[1] = stopEvent (signalled
        // for teardown), the rest = peer transport handles.
        std::array<HANDLE, 33> events{channelEvent, d->stopEvent};
        auto handleCount = d->peer->GetEventHandles(d->peer, events.data() + 2, 31);
        if (handleCount <= 0) {
            qCDebug(KRDPD) << "Unable to get transport event handles";
            break;
        }
        // Wait for something to happen on the connection.
        WaitForMultipleObjects(2 + handleCount, events.data(), FALSE, INFINITE);

        // Bail out before touching the peer transport if we were asked to stop,
        // so teardown stays race-free.
        if (stopToken.stop_requested()) {
            break;
        }

        // Read data from the socket and have FreeRDP process it.
        if (d->peer->CheckFileDescriptor(d->peer) != TRUE) {
            qCDebug(KRDPD) << "Unable to check file descriptor";
            break;
        }
    }

    qCDebug(KRDPD) << "Closing session";

    // Close the peer here, on the thread that owns it. Every other close path
    // just asks this thread to stop, so once a run thread exists this is the
    // only place the transport is driven - keeping teardown single-threaded.
    if (d->peer) {
        d->peer->Close(d->peer);
    }

    onClose();
}

bool DaemonRdpConnection::onCapabilities()
{
    return true;
}

bool DaemonRdpConnection::onActivate()
{
    setState(State::Activated);
    return true;
}

bool DaemonRdpConnection::onPostConnect()
{
    qCInfo(KRDPD) << "New client connected:" << d->peer->hostname << freerdp_peer_os_major_type_string(d->peer) << freerdp_peer_os_minor_type_string(d->peer);

    d->samFile.remove();

    rdpSettings *settings = d->peer->context->settings;
    const QString username = QString::fromLatin1(freerdp_settings_get_string(settings, FreeRDP_Username));

    if (d->server->usePAMAuthentication()) {
        if (!freerdp_settings_set_bool(settings, FreeRDP_AutoLogonEnabled, true)) {
            return false;
        }

        const QString password = QString::fromLatin1(freerdp_settings_get_string(settings, FreeRDP_Password));
        qCDebug(KRDPD) << "Attempting authenticating user with PAM";
        if (username == KUser().loginName() || KUser().loginName() == QStringLiteral("plasmalogin")) {
            if (pamAuthenticate(username, password) >= 0) {
                qCDebug(KRDPD) << "PAM authentication succeeded for user" << username;
                return true;
            }
        }
        const auto users = d->server->users();
        for (auto user : users) {
            if (user.password.isEmpty()) {
                return false;
            }
            if (user.name == username && user.password == password) {
                qCDebug(KRDPD) << "User" << username << "authenticated successfully";
                return true;
            }
        }
        return false;
    } else {
        // In the NLA case the user has been authorised against the SAM database
        return true;
    }
}

bool DaemonRdpConnection::onClose()
{
    setState(State::Closed);
    return true;
}

freerdp_peer *DaemonRdpConnection::rdpPeer() const
{
    return d->peer;
}

rdpContext *DaemonRdpConnection::rdpPeerContext() const
{
    return d->peer->context;
}
}

#include "moc_DaemonRdpConnection.cpp"
