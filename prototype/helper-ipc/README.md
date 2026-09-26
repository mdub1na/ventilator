# Read-only XPC status probe

Run `make smoke` on macOS to start temporary **user** launchd services, request a fixed status from a separate process, and remove the services. No `sudo` or SMC access is used. The ad hoc `helper-status serve` command refuses to start as root.

The only method is `fetchStatusWithReply` in `HelperStatus.h`. It takes no arguments and reports protocol version `1`, `read_only_prototype`, and false values for SMC access and write availability. The client rejects a different response or a five-second timeout.

The smoke script signs two copies ad hoc with different code identifiers. Each XPC peer requires the expected `cdhash`: the trusted copy connects, a different client binary cannot reach the listener, and the trusted client rejects a different server binary with XPC error `4102`. The script checks `launchctl bootout` and that each service is absent afterwards. If cleanup fails, it prints the service name and retains the temporary plist for inspection.

This is an IPC experiment, not a packaged root helper or a fan-control feature. A copied trusted binary has the same `cdhash`, so this test does **not** authenticate the developer or establish safe access to a root daemon. See [the service document](../../docs/services/helper-ipc-prototype.md).

Run `make package-smoke` to build a separate temporary `HelperProbe.app` containing the same read-only executable under `Contents/Resources` and a LaunchAgent plist under `Contents/Library/LaunchAgents`. The test app uses `SMAppService.agent` to register, query and unregister that user service; a separately signed client checks XPC. The script checks both signatures, the final `notRegistered` status and absence from `launchctl` before deleting the fixture. It never modifies `Ventilator.app` or registers a root LaunchDaemon. Its ad hoc signature is suitable only for this local probe.

Run `make daemon-prepare` outside the Codex sandbox to create a separate Apple Development signed `HelperDaemonProbe.app`. The command prints its absolute path as `prepared=...`. It selects a locally verifiable development identity, seals the read-only daemon and LaunchDaemon plist in the app, and checks both signing requirements. It never changes `Ventilator.app` or touches SMC. Keep the printed app path for the commands below:

```sh
cd prototype/helper-ipc
./signed-ipc-smoke.sh "$APP"
./daemon-probe.sh register "$APP"
./daemon-probe.sh status "$APP"
./daemon-probe.sh check "$APP"
./daemon-probe.sh unregister "$APP"
./daemon-probe.sh cleanup "$APP"
```

If `register` reports `Operation not permitted` and `status` says `requiresApproval`, approve the test background item in macOS System Settings, then run `status` and `check`. The app must stay in place until `unregister` verifies `notRegistered` and absence from `launchctl print system/com.ventilator.helper-ipc.signed-daemon-test`; `cleanup` enforces this. `check` accepts the expected signed client, rejects both an ad hoc client and a client with the same Team ID but another identifier, and confirms the system service. The daemon provides only the fixed status method, despite running as root. Its successful local probe does not establish a safe SMC control path.
