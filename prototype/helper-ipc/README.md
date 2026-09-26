# Read-only XPC status probe

Run `make smoke` on macOS to start temporary **user** launchd services, request a fixed status from a separate process, and remove the services. No `sudo` or SMC access is used. The ad hoc `helper-status serve` command refuses to start as root.

The ad hoc status server returns protocol version `1`, `read_only_prototype`, and false values for SMC access and write availability. It answers the no-argument `fetchBaselineWithReply` method with `unsupported_provider`; only the signed daemon implements the fixed-key SMC read. The client rejects a different response or a five-second timeout.

`make baseline-read-test baseline-contract-test` checks the system-state classifier and rejects a forged `baseline=true` response with nonzero targets. `SmcBaselineReadTest --live` makes one direct read-only snapshot on the supported Mac without `sudo`; this is a separate manual hardware check.

The three fixed temperature slots (`TCMz`, `Tg0D`, `TH0a`) contain a number only in the working 10–115 °C range. A finite reading outside that range becomes JSON `null` at its own slot in baseline, startup audit and watcher replies. This keeps fan-control state visible while marking the sensor unavailable. Client validators reject a raw out-of-range number. An actual SMC read error still fails the complete snapshot.

`make startup-audit-test` checks the fixed snapshot captured before the signed daemon opens its XPC listener. `Ventilator --helper-startup-audit` returns `system_at_start`, `changed_at_start`, or `read_failed` for that daemon PID. Every result has `control_allowed=false`; a single startup snapshot never proves sustained recovery. The diagnostic command's exit status confirms only a valid XPC response. The separate `integrated-probe.sh startup-audit-crash-run "$APP"` action asks for sudo in your Terminal before killing only the temporary read-only daemon and verifies a new PID with a later startup sample. See [the startup audit feature](../../docs/features/helper-startup-audit.md).

`integrated-probe.sh prepare-reboot` makes a signed test app in the ignored `.reboot-probes/` directory, which remains available across a Mac restart. Only this persistent test package adds `RunAtLoad=true` to its read-only LaunchDaemon plist. After `register` is enabled, run `eager-check "$APP"` **before** `check` or any other helper XPC command: it reads the running UID 0 PID from launchd before requesting the startup audit. Then run `check` and `reboot-before "$APP"`; the latter records the boot time and startup audit. Reboot the Mac yourself, then run `reboot-after "$APP"` as the first helper command. It requires a new boot time and a running UID 0 daemon before its first XPC request, then verifies the signed startup audit and a fresh system-state read. A PID may be reused after reboot, so boot time is the identity check. This proves only that the daemon was running when checked after login, before our first XPC request; it cannot establish when it started relative to login. Always finish with `unregister` and `cleanup`, then restore the background switch. This is a read-only lifecycle check, not a fan-control or SMC recovery test.

The smoke script signs two copies ad hoc with different code identifiers. Each XPC peer requires the expected `cdhash`: the trusted copy connects, a different client binary cannot reach the listener, and the trusted client rejects a different server binary with XPC error `4102`. The script checks `launchctl bootout` and that each service is absent afterwards. If cleanup fails, it prints the service name and retains the temporary plist for inspection.

This is an IPC experiment, not a packaged root helper or a fan-control feature. A copied trusted binary has the same `cdhash`, so this test does **not** authenticate the developer or establish safe access to a root daemon. See [the service document](../../docs/services/helper-ipc-prototype.md).

Run `make package-smoke` to build a separate temporary `HelperProbe.app` containing the same read-only executable under `Contents/Resources` and a LaunchAgent plist under `Contents/Library/LaunchAgents`. The test app uses `SMAppService.agent` to register, query and unregister that user service; a separately signed client checks XPC. The script checks both signatures, the final `notRegistered` status and absence from `launchctl` before deleting the fixture. It never modifies `Ventilator.app` or registers a root LaunchDaemon. Its ad hoc signature is suitable only for this local probe.

Run `make daemon-prepare` outside the Codex sandbox to create a separate Apple Development signed `HelperDaemonProbe.app`. The command prints its absolute path as `prepared=...`. It selects a locally verifiable development identity, seals the read-only daemon and LaunchDaemon plist in the app, and checks both signing requirements. It never changes the installed `Ventilator.app`. Keep the printed app path for the commands below:

```sh
cd prototype/helper-ipc
./signed-ipc-smoke.sh "$APP"
./daemon-probe.sh register "$APP"
./daemon-probe.sh status "$APP"
./daemon-probe.sh check "$APP"
./daemon-probe.sh unregister "$APP"
./daemon-probe.sh cleanup "$APP"
```

If `register` reports `Operation not permitted` and `status` says `requiresApproval`, approve the test background item in macOS System Settings, then run `status` and `check`. The app must stay in place until `unregister` verifies `notRegistered` and absence from `launchctl print system/com.ventilator.helper-ipc.signed-daemon-test`; `cleanup` enforces this. `check` accepts the expected signed client, rejects both an ad hoc client and a client with the same Team ID but another identifier, requests a fixed-key SMC snapshot, and confirms the system service. The daemon has no SMC write selector or write method. Its successful local probe does not establish a safe SMC control path.

For the main-app integration probe, run `./integrated-probe.sh prepare` from this directory. It builds a separate Apple Development signed copy of the real Compose `Ventilator.app`, embeds the same read-only daemon and a LaunchDaemon plist, then prints `prepared=...`. The app's own JVM process loads `libhelper-probe.dylib` and provides manual `--helper-request`, `--helper-baseline`, `--helper-watch-start`, and `--helper-watch-status` commands. The latter methods read only fixed fan and temperature keys; callers cannot choose keys. Use the probe commands below:

```sh
./integrated-probe.sh status "$APP"
./client-lifecycle-smoke.sh "$APP" # before root daemon registration
./integrated-probe.sh register "$APP"
./integrated-probe.sh check "$APP"
./integrated-probe.sh watch "$APP" # waits for 61 read-only snapshots; about one minute
./integrated-probe.sh ui-crash "$APP"
./integrated-probe.sh restart-before "$APP"
# In a separate Terminal, run the sudo launchctl kill command printed above.
./integrated-probe.sh restart-after "$APP"
./integrated-probe.sh sleep-before "$APP"
# Sleep the Mac and wake it again before continuing.
./integrated-probe.sh sleep-after "$APP"
./integrated-probe.sh unregister "$APP"
./integrated-probe.sh cleanup "$APP"
```

For a **separate** in-flight root request probe, first reach `enabled` and pass `check`, then run `./root-inflight-smoke.sh "$APP"` from a Terminal as the logged-in user. It asks for `sudo` in that Terminal, sends `SIGSTOP` and `SIGKILL` only to `system/com.ventilator.helper-ipc.read-only`, requires the pending client request to fail without a status, then requires a new signed request from a new UID 0 daemon PID. The script resumes a stopped test daemon on ordinary errors. Keep a second Terminal ready with `sudo launchctl kill SIGCONT system/com.ventilator.helper-ipc.read-only` if the script is forcibly terminated. Always run `integrated-probe.sh unregister "$APP"` and `cleanup "$APP"` afterward and restore the background switch. Neither process has an SMC writer.

If macOS reports `requiresApproval`, approve the new Ventilator background item in System Settings before `check`. `check` requests the exact four-field status and a fixed-key SMC snapshot from the **main app process**, rejects an ad hoc client and another signed identifier, then confirms a UID 0 system service. If any step fails, keep the package until `unregister` confirms `notRegistered` and `launchctl` absence. Normal UI launches never register or query this experimental daemon. No SMC writer is bundled.

`watch` starts a bounded read-only loop in the daemon, lets the initiating JVM process exit, then polls from new JVM processes until the daemon reports `stable` or a bounded timeout. It requires 61 baseline snapshots, second 60, and an unchanged UID 0 daemon PID. It reports an error on `changed`, read failure, or timeout; it never attempts SMC recovery. On Mac15,7/macOS 27.0 the completed interval lasted 66.09 seconds, ended `stable`, and cleanup restored the system service and background switch to their prior states. See [the watch feature](../../docs/features/helper-baseline-watch.md).

For a separate read-only daemon crash probe, first reach `enabled`, then run `./integrated-probe.sh watch-crash-run "$APP"` yourself in Terminal. It asks `sudo` to authenticate **before** starting the watcher, confirms that it is still running, then immediately sends `SIGKILL` to the exact test service `system/com.ventilator.helper-ipc.read-only`. It never targets the ordinary Ventilator UI or writes SMC. The new UID 0 daemon must return `idle` with zero samples instead of a stale `stable`; a fresh watch must then complete all 61 samples. The underlying `watch-crash-before` and `watch-crash-after` actions are also available separately for diagnosis. A failure retains a marker for inspection; always `unregister` and `cleanup` before deleting the temporary app, and restore the macOS background switch.

Before registration, `client-lifecycle-smoke.sh` temporarily advertises the same Mach service name in the **user** bootstrap domain with the correctly signed daemon. Its signed control client must reach that service, while the main app must reject it because its `NSXPCConnection` uses `NSXPCConnectionPrivileged`. The script then suspends the user daemon during a request, kills it, requires that request to fail, explicitly restarts the daemon, and requires a fresh request to succeed. It removes the LaunchAgent even on failure; a `CRITICAL` removal error retains the plist for investigation. This tests client failure handling without root and does not prove a root in-flight interruption.

After the root daemon is enabled, `ui-crash` launches only this temporary signed UI, kills its exact process, verifies its status item child exits, and requires the same root daemon PID and a new trusted XPC response. The temporary app may briefly show its window and menu-bar icon. It does not alter the ordinary installed Ventilator app.

If the background switch is on but the status remains `requiresApproval`, run `unregister`, wait for that operation to settle, then run `register` again and check for `enabled`. This sequence was needed for the second local probe. Never delete the signed package while a registration is pending; `cleanup` checks that it was removed. Apple DTS also describes a delay after `SMAppService.unregister` before re-registration in [this discussion](https://developer.apple.com/forums/thread/783539).

The optional `restart-before` records the running read-only daemon PID and prints the exact `sudo launchctl kill SIGKILL` command for that test service. macOS requires an administrator password in Terminal; never put it in this script or send it to another process. `restart-after` then requires a new root PID, `enabled` registration, and a successful signed XPC request. It never touches the ordinary Ventilator UI or fan control. A failed restart still requires `unregister` before `cleanup`. The two sleep commands record the current boot and `pmset` sleep/wake count, then require a new cycle in the same boot session, `enabled` registration, a signed response, and a UID 0 daemon. Sleep and wake the Mac yourself between them. A successful post-wake response shows that this stateless status service is available again; it does not establish any SMC recovery behavior.

Both lifecycle checks passed on Mac15,7/macOS 27.0: a new root process answered after the administrator terminated the previous daemon, and a signed request succeeded after a recorded sleep/wake cycle in the same boot session. The temporary service and app were removed, and the background switch returned to off. These checks use fresh requests; the separate in-flight result is recorded below.

The domain-shadow and UI-crash probes also passed on Mac15,7/macOS 27.0. The rebuilt main app rejected a same-named signed user LaunchAgent, then accepted two fixed replies from the root daemon. After the temporary UI was killed, its status-item child exited; the root daemon kept its PID and answered a fresh request. `unregister` verified `notRegistered` and system-service absence, the package was removed, and the background switch returned to off.

The separate root in-flight probe passed on Mac15,7/macOS 27.0. The request remained pending while the test daemon was stopped; after its `SIGKILL`, the client reported an XPC connection error without accepting a status. A fresh request automatically received the fixed read-only response from a different UID 0 PID (`85675` to `86052`). Independent inspection found the new daemon running and the old PID absent. Registration was removed, the temporary app deleted, and the background switch restored to off. This does not test SMC access or fan-state recovery.
