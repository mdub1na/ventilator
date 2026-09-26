# Read-only XPC status probe

Run `make smoke` on macOS to start a temporary **user** launchd service, request a fixed status from a separate process, and remove the service. No `sudo` or SMC access is used. The server refuses to start as root because it does not authenticate clients.

The only method is `fetchStatusWithReply` in `HelperStatus.h`. It takes no arguments and reports protocol version `1`, `read_only_prototype`, and false values for SMC access and write availability. The client rejects a different response or a five-second timeout.

The smoke script checks `launchctl bootout` and that the service is absent afterwards. If cleanup fails, it prints the service name and retains the temporary plist for inspection. This is an IPC experiment, not a packaged helper or a fan-control feature. See [the service document](../../docs/services/helper-ipc-prototype.md).
