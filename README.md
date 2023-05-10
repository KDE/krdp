# KRdp

Library and examples for creating an RDP server.

# Running the example server

The example server requires a username and password to be provided on the command line. The username and password are used when logging in from an RDP client. They can be provided using the `-u` and `-p` command line parameters, respectively. In addition, a valid TLS certificate and key are required to encrypt the communication between client and server. By default, the server will look for a file called `server.crt` and `server.key` in the current working directory, but a different path can be provided using the `--certificate` and `--certificate-key` command line parameters.

For example, to run the server using the user `test` and the password `test`, using a TLS certificate and key with the default name in the current working directory, you would run the command `krdp_server -u test -p test`. The server will listen on all interfaces on port 3389.

## Creating a TLS certificate
A self-signed certificate can be generated using OpenSSL using the following command: `openssl req -nodes -new -x509 -keyout server.key -out server.crt -days 1000`. It will ask for some information, which can be provided if you wish or you can accept the default values.

# Connecting to the example server

Currently, the main client that has been used for testing and is confirmed to work is the FreeRDP client. To connect to the server, make sure to pass the username and password the server was started with. Launch the client with the following command: `xfreerdp /u:<username> /p:<password> /gfx -clipboard /v:<ip_address>:3389`, filling in the username, password and IP address as appropriate.
