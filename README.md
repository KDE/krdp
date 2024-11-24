# KRdp

Library and examples for creating an RDP server.


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

# Running the example server

The example server requires a username and password to be provided on the command line, which will be used when connecting from an RDP client. They can be provided using the `-u` and `-p` command line parameters, respectively. For example:

```
krdpserver -u user -p test
```

The server will then listen on all interfaces on port 3389, and clients can connect with the username "user" and the password "pass".

# Connecting to the example server

To connect to the server, make sure to pass the username and password the server was started with. Note that the username is case-sensitive; this may be especially unexpected for those using Microsoft Windows RDP clients to connect, as system usernames on that platform are generally not case-sensitive.

Currently, the main client that has been used for testing and is confirmed to work is the FreeRDP client. Launch the FreeRDP client with the following command: `xfreerdp /u:<username> /p:<password> -clipboard /v:<ip_address>:3389`, filling in the username, password and IP address as appropriate. If testing locally, substitute `localhost` for an IP address.

# Security considerations

In addition, a valid TLS certificate and key are required to encrypt the communication between client and server. The server will look for a file called `server.crt` and `server.key` in the current working directory, but a different path can be provided using the `--certificate` and `--certificate-key` command line parameters. If no valid certificate is found using any of these methods, the server will internally generate a self-signed certificate and use that.

# Command Line Options

The following command line options are available for the example server:

<dl>
    <dt>-u, --username <username></dt>
    <dd>The username to use when a client tries to login. Required.</dd>
    <dt>-p, --password <password></dt>
    <dd>The password to require when a client tries to login. Required.</dd>
    <dt>--port <port></dt>
    <dd>The port to listen on for connections. Defaults to 3389.</dd>
    <dt>--certificate <certificate></dt>
    <dd>The path to a TLS certificate file to use. If not supplied or it cannot be found a temporary self-signed certificate will be generated.</dd>
    <dt>--certificate-key <certificate-key></dt>  
    <dd>The path to the TLS certificate key that matches the provided certificate.</dd>
    <dt>--monitor <monitor></dt>The index of the monitor to use for streaming video. If not supplied the whole workspace is used.</dd>
    <dt>--quality <quality></dt>
    <dd>Set the video quality, from 0 (lowest) to 100 (highest).</dd>
</dl>

# Known Working and Not-Working Clients

The following clients are known to work with the server:

- XFreeRDP and wlFreeRDP from the FreeRDP project.
- Remmina, a remote desktop client for Gnome.
- Thincast Remote Desktop Client
- Windows Remote Desktop client, at least as shipped with a recent Windows 10.

The following clients are known not to work:

- Microsoft's Remote Desktop client for Android. While it should support H.264
it seems to not enable it.

# Known Issues and Limitations

- Only video streaming and remote input is supported.
- Only the NLA security type of RDP is supported.
- Only one username and password combination is supported for login.
- Only the "Graphics Pipeline" extension of the RDP protocol is
implemented for video streaming. This extension allows using H.264 for video
streaming, but it means only clients supporting that extension are supported.
- H.264 encoding is done using hardware encoding if possible, but currently we
only support using VAAPI for this. Most notably this means hardware encoding on
NVidia hardware can not be used and software encoding will be used instead.
Additionally, on certain hardware there are limits to what size of frame can be
encoded by the hardware. In both cases, encoding will fall back to software
encoding.
- KDE's implementation of the Remote Desktop portal is rather limited as
shipped with Plasma 5.27. Most notably it does not allow selecting which screen
to stream, nor does it have an option to remember the setup and reuse it when
the same application requests a new connection. As a workaround, the server
will open a remote desktop session on startup and reuse that session for all
RDP connections. Additionally, monitor selection can be done using the
`--monitor` command line option.
- Input on a high DPI screen may be offset incorrectly. This is due to a bug in
the Remote Desktop Portal that has been fixed in the meantime. The fix will be 
released with KDE Plasma 5.27.8.
