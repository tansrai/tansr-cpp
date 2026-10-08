# Native dependency notices

The SDK uses the dependency versions fixed in `dependencies.json`:

| Dependency | Purpose | License text |
|---|---|---|
| curl 8.22.0 | HTTP/SSE transport; Schannel on Windows, OpenSSL on the prepared Linux build | `licenses/curl-8.22.0-COPYING` |
| c-ares 1.34.8 | curl asynchronous DNS | `licenses/c-ares-1.34.8-LICENSE.md` |
| OpenSSL 3.5.9 | SHA-256, secure randomness, AES-GCM and platform-dependent TLS backend | `licenses/openssl-3.5.9-LICENSE.txt` |

The copied texts are preserved from the verified dependency sources. Binary SDK artifacts bundle matching development dependencies under `dependencies/`; demo artifacts retain notices for their linked dependencies. The C++ SDK uses its own owned JSON representation; nlohmann JSON is not a linked runtime dependency. Build-only tools are not redistributed in these SDK recipes. Other builds that enable additional dependencies must carry their corresponding notices as well.

The Tansr SDK, demos, public documentation and approved public contract subset use the MIT License in the root `LICENSE`. This does not relicense Serve/kernel source, internal references or private history. See `RIGHTS.txt` for the distribution boundary.
