# core_smtp

Production-oriented SMTP persistence and a bounded synchronous transport primitive. This component is intentionally not connected to HTTP or `main`; a future core-0 owner task should serialize calls to `send()`.

## Security model

- Configuration is serialized with fixed bounds and persisted only through `core_security::StorageService` as `Namespace::Configuration` / `RecordType::SmtpConfig`.
- `ConfigRepository::save()` supports redacted-password updates: set `password_redacted=true` and `password_length=0` to preserve the existing encrypted password. The caller's password buffer is wiped on every return path.
- `tls` performs implicit TLS. `start_tls` requires a `220` STARTTLS response and a successful TLS handshake before authentication. Both use the ESP-IDF certificate bundle and hostname verification (`common_name=host`, `skip_common_name=false`).
- `plain_lan_only` is explicit and accepts only literal RFC1918 IPv4, IPv4/IPv6 link-local, unique-local IPv6, or loopback targets. DNS names and public literals are rejected. AUTH is always refused without TLS.
- Headers/envelope values reject CR/LF. Reply lines and aggregate replies are bounded. Message and recipient limits match `web/src/lib/request-contracts.ts`. DATA is CRLF-normalized and dot-stuffed.
- Credentials, message content, and server replies are never logged. Temporary plaintext repository buffers, encoded credentials, and DATA scratch are zeroized.

## Limits

| Value | Maximum bytes/count |
|---|---:|
| Host | 253 bytes |
| Username | 128 bytes |
| Password | 256 bytes |
| Email address | 254 bytes |
| Subject | 160 bytes |
| Body | 4096 bytes |
| Recipients | 4 |
| Reply line / reply | 512 / 4096 bytes |

## API notes

Call `ConfigRepository::start()` before `load()` or `save()`. A loaded `Config` contains the password because transport needs it; callers must keep it task-local and call `clear_config()` immediately after use. Do not expose it through status or HTTP responses.

`send()` is blocking but every connect/read/write has a caller-bounded timeout. It is suitable as the low-level operation of a future bounded core-0 queue owner, not for direct use in callbacks. It supports `AUTH LOGIN`, only after TLS. SMTP capability negotiation is deliberately conservative: unavailable STARTTLS or rejected commands fail closed.

The public `ReplyParser`, validation functions, LAN policy helper, and `encode_data_body()` are independent enough for host/unit tests. No owner task, HTTP route, retry queue, or application integration is included here.
