# KRdp

Library and examples for creating an RDP server.

## Creating a certificate
`openssl req -nodes -new -x509 -keyout server.key -out server.crt -days 1000`

## Connecting to it
`xfreerdp /u:test /p:test /sec:nla /gfx -clipboard /v:localhost:3389`
