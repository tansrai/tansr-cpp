# Tansr C++ SDK contributor guide

This repository contains the native C++17 SDK and public API demos under MIT.
Serve owns the agent core; this client must not silently execute tools on the Serve host.

- Preserve the frozen UAPI revision 7 contract bytes and generated operation/schema tables.
- This public distribution contains 20 approved contract assets. Explicitly use
  python tools/contract_check.py --mode public,
  python tools/generate_api.py --mode public --check, and
  python tools/contract_check_test.py --mode public.
  Never infer a different contract mode from missing files.
- Follow the CMake/CTest and tools/CI.md build and installation instructions.
  Integration drivers require an explicitly supplied compatible Serve fixture and
  its provenance; no private fixture or credentials are distributed here.
- Keep callback ownership, cancellation, authorization, durable recovery and tool
  output receipt boundaries intact. Add focused regression coverage for changes.
- Do not commit credentials, build outputs, runtime logs or user archives.
  Source and binary release steps are documented in packaging/RELEASE.md.
- A source export is a reviewed snapshot, not proof that a tag, package or public
  registry release has already been published.
