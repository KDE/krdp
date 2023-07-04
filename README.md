# KRdp

Library and examples for creating an RDP server.

# Running the example server

The example server requires a username and password to be provided on the command line, which will be used when connecting from an RDP client. They can be provided using the `-u` and `-p` command line parameters, respectively. For example:

```
krdpserver -u user -p test
```

The server will then listen on all interfaces on port 3389, and clients can connect with the username "user" and the password "pass".

# Connecting to the example server

To connect to the server, make sure to pass the username and password the server was started with.

Currently, the main client that has been used for testing and is confirmed to work is the FreeRDP client. Launch the FreeRDP client with the following command: `xfreerdp /u:<username> /p:<password> /gfx -clipboard /v:<ip_address>:3389`, filling in the username, password and IP address as appropriate. If testing locally, substitute `localhost` for an IP address.

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
</dl>
