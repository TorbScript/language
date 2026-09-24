/*
 * tls_certificates.h - the certificates of the TLS tests: a test root, a certificate for `localhost` and `127.0.0.1`
 * it signed with its key, and a second root that signed nothing. P-256 keys, valid until 2126; made with openssl once,
 * and nothing but these tests trusts them.
 */

#ifndef TORB_TLS_CERTIFICATES_H
#define TORB_TLS_CERTIFICATES_H

static const char test_root[] =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIBpjCCAUugAwIBAgIUX0ocJiSTMF4EyI948Ywl9dBFuIgwCgYIKoZIzj0EAwIw\n"
  "HzEdMBsGA1UEAwwUVG9yYlNjcmlwdCBUZXN0IFJvb3QwIBcNMjYwOTI0MTgwMDA2\n"
  "WhgPMjEyNjA4MzExODAwMDZaMB8xHTAbBgNVBAMMFFRvcmJTY3JpcHQgVGVzdCBS\n"
  "b290MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEnoU8ZlkjCCHuMztPRGCb7EKo\n"
  "y7KLXmMddFh+kw1uKQKo18Dtx4zsURLEMCqklHgEARKvsbTVjXtzvfqkxXUWzKNj\n"
  "MGEwHQYDVR0OBBYEFBz/+wIsZils42wBpvVRftEGr2wwMB8GA1UdIwQYMBaAFBz/\n"
  "+wIsZils42wBpvVRftEGr2wwMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQD\n"
  "AgEGMAoGCCqGSM49BAMCA0kAMEYCIQCYnzkVoRbZZ3bCLbuSRf+yC46iVY+2ACKV\n"
  "ttODjyX9TgIhAPq6TrV57XFD683pgB37633cYgqQOLTlf0+5FJ3Dgu1W\n"
  "-----END CERTIFICATE-----\n";

static const char test_server_certificate[] =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIByDCCAW2gAwIBAgIUWW3sNf3SaLIOaLhiA3l8ZCE7EugwCgYIKoZIzj0EAwIw\n"
  "HzEdMBsGA1UEAwwUVG9yYlNjcmlwdCBUZXN0IFJvb3QwIBcNMjYwOTI0MTgwMDA3\n"
  "WhgPMjEyNjA4MzExODAwMDdaMBQxEjAQBgNVBAMMCWxvY2FsaG9zdDBZMBMGByqG\n"
  "SM49AgEGCCqGSM49AwEHA0IABEXvBfnFsZ5jZuUd75E2cE7UrRYne0C63+Fxn5Dr\n"
  "LvdEOFnewKfyQ1fSkK3wTe8HZswifmx1YYrX2LX/uB7QD8ijgY8wgYwwCQYDVR0T\n"
  "BAIwADAOBgNVHQ8BAf8EBAMCB4AwEwYDVR0lBAwwCgYIKwYBBQUHAwEwGgYDVR0R\n"
  "BBMwEYIJbG9jYWxob3N0hwR/AAABMB0GA1UdDgQWBBR5X0QnapZcnsquwu+THlWj\n"
  "wWAAMzAfBgNVHSMEGDAWgBQc//sCLGYpbONsAab1UX7RBq9sMDAKBggqhkjOPQQD\n"
  "AgNJADBGAiEAuFtnGP9Pd3gKzfzkSNvnYnCy0fJzaXiBlIiLmbs+P7MCIQClGtvD\n"
  "ds8yyzK7J9iXnKnTwH4dd92UaqZcruOde6Ozgg==\n"
  "-----END CERTIFICATE-----\n";

static const char test_server_key[] =
  "-----BEGIN PRIVATE KEY-----\n"
  "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg5JAlAbPbwYok2Sif\n"
  "prLNFLAwadIBxYtUO1YQRe2Awp6hRANCAARF7wX5xbGeY2blHe+RNnBO1K0WJ3tA\n"
  "ut/hcZ+Q6y73RDhZ3sCn8kNX0pCt8E3vB2bMIn5sdWGK19i1/7ge0A/I\n"
  "-----END PRIVATE KEY-----\n";

static const char test_other_root[] =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIBhDCCASugAwIBAgIUNcppcxsoJh3LC4SuNV2905it3z8wCgYIKoZIzj0EAwIw\n"
  "FzEVMBMGA1UEAwwMQW5vdGhlciBSb290MCAXDTI2MDkyNDE4MDAwN1oYDzIxMjYw\n"
  "ODMxMTgwMDA3WjAXMRUwEwYDVQQDDAxBbm90aGVyIFJvb3QwWTATBgcqhkjOPQIB\n"
  "BggqhkjOPQMBBwNCAATLN2hr7FX0WCIiSRX1v5bZC60+2bFil4GqNtO0DcKfuo5z\n"
  "WtIeQIcdDMzuCEdgJfrRCqcXm/xWKegAYjx4330bo1MwUTAdBgNVHQ4EFgQUh8D0\n"
  "fw0tYqO2TaSAwEphjdSdT9AwHwYDVR0jBBgwFoAUh8D0fw0tYqO2TaSAwEphjdSd\n"
  "T9AwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAgNHADBEAiBWPzUV+BUG2+cM\n"
  "31/S88kys7fpZvwRMg/vOlUArTwroAIgfP1AbNwjvS0UJUpnGX/y2WpXBFm9aF+j\n"
  "R/XhisYKo4c=\n"
  "-----END CERTIFICATE-----\n";

#endif /* TORB_TLS_CERTIFICATES_H */
