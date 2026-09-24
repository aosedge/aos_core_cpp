# TLS test credentials

These public test-only credentials are used for local TLS tests. Never use them in deployments.

`localhost.pem` is a self-signed test CA and initial endpoint certificate.
`rotated.pem` is signed by that CA, uses the same test key, and has a different common name;
both certificates have `DNS:localhost` in SAN.
`localhost.key` is deliberately committed solely for tests.
