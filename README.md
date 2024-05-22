# KRdp

KRdp is a library implementing the necessary supporting code to use FreeRDP
with a KDE Plasma session. In addition, there is a server that makes use of
this library to expose a running Plasma session through RDP and a KCM that
allows one to configure the server.

KRdp supports receiving input from a client and sending video to the client.
Video is implemented as an H.264 video stream using the Graphics Pipeline
protocol extension, which means your client requires support for that. For
authentication, only the NLA protocol is supported, which is the most modern
and secure. It also means all traffic is encrypted, since NLA requires using a
TLS connection.

# The Library

The main entrypoint for the library is `KRdp::Server`, which creates a
listening socket and listens for incoming connections. For each incoming
connection, a new instance of `KRdp::RdpConnection` is created.
`KRdp::RdpConnection` handles the core RDP communication with a specific
client, making use of separate classes for other parts of the protocol.

`KRdp::RdpConnection` needs to be fed new frames to send to the client. To help
with that, an instance of a subclass of `KRdp::AbstractSession` can be created.
There are currently two possible subclasses, `KRdp::PortalSession` and
`KRdp::PlasmaScreencastV1Session`. `KRdp::PortalSession` will use the
FreeDesktop Remote Desktop portal to request a video stream for the server and
also use that portal to send the client's input to the server.
`KRdp::PlasmaScreencastV1Session` does the same but using the Plasma Screencast
wayland protocol, which may be restricted for security or not be implemented.

# The Server

The server is a simple binary that makes use of the library to expose the
current Plasma session over the RDP protocol. It can be run standalone or
started as a user service through systemd. By default it will use the Remote
Desktop portal for video streaming and input.

# Remote Desktop KCM

![Remote Desktop Settings Window](https://cdn.kde.org/screenshots/krdp/krdp-settings.png)

Remote Desktop System Settings page (KCM) that lives in the Networking category.

Features:
- User can toggle the server (running the`krdpserver` binary) on and off using a toggle switch.
- The server can be set to auto-start at session login.
- The KCM uses SystemD DBus messages to toggle the server on and off and auto-start it.
- User can easily add, modify, and remove usernames and passwords that are allowed to connect to the server.
- User can change the port of the server.
    - Do note that the address is currently set to `0.0.0.0`, which means any interface that accepts connections for `krdpserver` will work.
- Certificates can be auto-generated (this is done by default), or the user can supply their own certificates.
- Video quality can be changed between responsiveness and quality.
    - Do note that in software encoding mode, the quality slider might not necessarily do anything. This seems to be an encoder issue.
- The KCM will do some basic sanity-checking and warn the user about the following issues:
    - Password manager inaccessible (for KRDP user passwords)
    - No supported H264 encoder
    - Failures with generating certificates
