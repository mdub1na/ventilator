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

For the main-app integration probe, run `./integrated-probe.sh prepare` from this directory. It builds a separate Apple Development signed copy of the real Compose `Ventilator.app`, embeds the same read-only daemon and a LaunchDaemon plist, then prints `prepared=...`. The app's own JVM process loads `libhelper-probe.dylib` and provides these manual commands:

```sh
./integrated-probe.sh status "$APP"
./integrated-probe.sh register "$APP"
./integrated-probe.sh check "$APP"
./integrated-probe.sh restart-check "$APP"
./integrated-probe.sh sleep-before "$APP"
# Sleep the Mac and wake it again before continuing.
./integrated-probe.sh sleep-after "$APP"
./integrated-probe.sh unregister "$APP"
./integrated-probe.sh cleanup "$APP"
```

If macOS reports `requiresApproval`, approve the new Ventilator background item in System Settings before `check`. `check` requests the exact four-field status from the **main app process**, rejects an ad hoc client and another signed identifier, then confirms a UID 0 system service. If any step fails, keep the package until `unregister` confirms `notRegistered` and `launchctl` absence. Normal UI launches never register or query this experimental daemon. No SMC writer is bundled.

The optional `restart-check` terminates **only this test daemon** with `launchctl kill SIGKILL`, then requires a new root PID and a successful signed XPC request. It never touches the ordinary Ventilator UI or fan control. Run it only while the signed read-only service is `enabled`; a failed restart still requires `unregister` before `cleanup`. The two sleep commands record the current boot and `pmset` sleep/wake count, then require a new cycle in the same boot session, `enabled` registration, a signed response, and a UID 0 daemon. Sleep and wake the Mac yourself between them. A successful post-wake response shows that this stateless status service is available again; it does not establish any SMC recovery behavior.
