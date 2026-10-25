# Core Pilot security contracts

`core_security` handles _bytes_, not application object schemas. `InternalSeedStore`
represents the dedicated internal-flash seed partition; `EspSeedPartition` checks
its label, offset (`0x920000`) and size (`0x10000`) before access. The 64-byte
record is `CPSEED01`, 32 seed bytes, little-endian checksum (starting at
`0xa5a55a5a`, rotate-left 5 then XOR each byte), and 20 `0xff` bytes. Only a
fully erased record permits seed generation. An invalid record or I/O error must
**not** cause regeneration; losing a seed makes existing SD records unreadable.
The checksum is for corruption detection, not authentication. Factory reset
must explicitly erase and verify the _whole_ partition. The ESP adapter checks
raw flash for the erased state (encrypted-flash reads can decrypt erased bytes
into non-`0xff` data), but uses decrypted partition reads for stored records.
Ensure ESP-IDF's hardware
RNG has a true entropy source enabled before first seed generation; `esp_fill_random`
by itself does not establish that precondition.

HKDF-SHA256 uses salt `CorePilot Device Seed HKDF v1` and the namespace-specific
`core_pilot:v1:*` info label. Records use ChaCha20-Poly1305 with 12 random nonce
bytes and a 16-byte tag. The AAD is `CPER`, format version byte `01`, enum
record-type and namespace ordinal bytes, generation as 8 little-endian bytes,
schema as 2 little-endian bytes, and the exact path bytes (no NUL). The on-disk
record is `CPER` followed by postcard's Serde struct field order: format byte,
postcard varint enum ordinal for record type and namespace, postcard varint
`u64` generation, postcard varint `u16` schema, 12 nonce bytes, postcard varint
ciphertext length, then ciphertext and appended tag. Record parsing rejects
noncanonical varints, trailing data, unknown enum variants, oversized lengths
and malformed or unauthenticated inputs. Maximum plaintext is 1 MiB; this
component additionally bounds the path to 1024 bytes and requires caller-owned
bounded buffers. Never retry a failed authentication as plaintext.

`EncryptedStore` is intentionally abstract: the application storage service must
supply a backend with atomic replacement, strict length checks, and no plaintext
writes. `StorageService` authenticates path and metadata and zeroizes its
ciphertext scratch on exit. Its `put`/`get` operate on **already postcard-encoded
application values**; every postcard payload schema needs an independently
verified codec or explicit type-specific encoding.
Do not treat native C++ struct bytes or JSON as compatible postcard payloads.

`core_storage` uses the internal `journal` NVS partition as the authority for
SD replicas. A per-path marker is committed **before** changing a flash record;
a missing tracked record cannot be re-imported from SD, including after archive.
Existing flash records are adopted on first read. Flash remains authoritative and
NVS blob replacement retains its prior power-loss behavior; SD `.BAK` recovery
remains available for untracked legacy records. An interrupted first write may
leave a marker without a record, failing closed rather than accepting stale SD.
This does **not** protect against rollback of the internal journal, an attacker
rewriting both NVS and SD, direct use of `SdEncryptedStore` without the replicated
backend, or an old authentic record on initial legacy import. It also does not
bind individual flash ciphertext versions: NVS must remain trusted. No eFuse
counters are burned. Keep the journal partition when upgrading.

Production defaults disable core dumps; this does not erase plaintext dumps
previously written to the `crash` partition. Existing devices need an explicit
safe migration/erase plan. A pre-existing `firmware/s3/sdkconfig` can override
`sdkconfig.defaults` until regenerated with the new choice.

ESP-IDF's mbedTLS configuration must enable `CONFIG_MBEDTLS_CHACHA20_C`,
`CONFIG_MBEDTLS_POLY1305_C`, `CONFIG_MBEDTLS_CHACHAPOLY_C`, and
`CONFIG_MBEDTLS_HKDF_C` before linking this component. The checked-in
`sdkconfig` currently disables these; enabling them requires a separate
configuration change outside this component. Unity cases in `test/` can be run
as an ESP-IDF unit-test component on a configured target; memory fakes keep the
seed/storage cases independent of actual flash hardware. No hardware validation
is implied by these tests.
