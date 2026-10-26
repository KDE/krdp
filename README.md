# KRDP

KRDP provides a Remote Desktop Protocol server for the KDE Plasma desktop, allowing you to connect to and control your computer remotely.

See also: [Testing KRDP](TESTING.md)

## Getting Started

To begin using KRDP, navigate to _Remote Desktop_ in _System Settings_' _Security & Privacy_ category.

![Remote Desktop Settings](https://cdn.kde.org/screenshots/krdp/krdp-settings.png)

You can configure:
 - The **user identity** used to connect to the server
    - _Including support for using the credentials of the host user, but also for custom username and passwords._
 - Whether the server is **automatically started** on login
 - The **port** the server will listen on
 - Whether to **share the current desktop or allow only one session** at a time (with the remote user seeing a virtual display)
 - The **quality** of the remote desktop connection
 - Whether to use a custom TLS certificate
    - _A default self-signed certificate will be automatically generated for you if a custom certificate & key is not specified._

To start the RDP server, enable it in the top-right. The local IPv4/IPv6 address for the server will be indicated, and you can now connect to your computer remotely using a remote desktop client.

## Advanced

The KRDP service can be controlled from the command-line:
```bash
systemctl --user enable --now app-org.kde.krdpserver.service
systemctl --user status app-org.kde.krdpserver.service
```

The remote desktop server, `krdpserver`, can be started manuallly from the command-line. It will use the same settings as the service managed by the KCM, overidden by command-line arguments. Use `krdpserver --help` for a complete listing. 

If the default TLS certificate path is not overridden, KRDP will generate a default self-signed certificate and key at that location. If a custom TLS certificate is specified, KRDP will not generate anything or fallback to using the default certificate, and will instead fail.

## RDP Client Support

The following clients are known to work with the server:

 - `xfreerdp` and `wlfreerdp`, as provided by the FreeRDP project
 - Remmina, a remote desktop client for Gnome
 - Thincast Remote Desktop Client
 - Windows' Remote Desktop Connection (for at least Windows 10)
 - Microsoft's Android Remote Desktop client, confusingly named "Windows App"

## Known Issues

 - KRDP is tied to the user, and so requires a logged-in user session to work. For remote access, this is not always desirable.
    - You can work around this by configuring SDDM or Plasma Login Manager to automatically log in. Your system will be unlocked on start-up.
 - Windows' Remote Desktop Connection requires credentials specified ahead-of-time when the system user is enabled as NLA is not supported/possible in this configuration.
