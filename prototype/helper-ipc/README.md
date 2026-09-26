# Read-only XPC status probe

Run `make smoke` on macOS to start temporary **user** launchd services, request a fixed status from a separate process, and remove the services. No `sudo` or SMC access is used. The server refuses to start as root because this stage has no trusted signing identity.

The only method is `fetchStatusWithReply` in `HelperStatus.h`. It takes no arguments and reports protocol version `1`, `read_only_prototype`, and false values for SMC access and write availability. The client rejects a different response or a five-second timeout.

The smoke script signs two copies ad hoc with different code identifiers. Each XPC peer requires the expected `cdhash`: the trusted copy connects, a different client binary cannot reach the listener, and the trusted client rejects a different server binary with XPC error `4102`. The script checks `launchctl bootout` and that each service is absent afterwards. If cleanup fails, it prints the service name and retains the temporary plist for inspection.

This is an IPC experiment, not a packaged helper or a fan-control feature. A copied trusted binary has the same `cdhash`, so this test does **not** authenticate the developer or establish safe access to a root daemon. See [the service document](../../docs/services/helper-ipc-prototype.md).
