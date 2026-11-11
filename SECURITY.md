# Security policy

EverEye is a local Linux utility. It keeps session state in the user's state directory, communicates with power services through the user's D-Bus session and system logind/UPower interfaces, and does not require a root helper.

## Reporting a vulnerability

Please report security issues privately through the repository's security reporting channel. Include the EverEye version, distribution, desktop session, architecture, exact reproduction steps, and any relevant logs with personal paths or identifiers removed. Do not publish an exploitable proof of concept in a public issue before a fix is available.

If the hosting repository does not expose a private reporting channel, open a minimal public issue requesting a private contact and omit secrets, personal data, and weaponized details.

## AI jobs and the remote gateway

EverEye can run commands you select as supervised jobs and, when you explicitly start
`evereye gateway`, expose them to your phone over HTTP. That gateway is **remote command
execution by design**. It is off unless started, requires a bearer token on every request,
binds loopback by default, and refuses an off-loopback bind without TLS (unless
`EVEREYE_INSECURE_ALLOW_PLAINTEXT=1` is set). `allowArbitraryCommands` is false by default so
only `~/.config/evereye/tasks.json` presets can be started remotely. Jobs run as the invoking
user and are not sandboxed. Keep the token secret, prefer a VPN off your home network, and do
not expose the gateway to the public internet. The Flatpak sandbox has no network access, so
the gateway is not available in the Flatpak package in this release.

## Scope

Reports involving session ownership, D-Bus authorization, inhibitor release, journal
permissions, watchdog targeting, job supervision, gateway authentication or request parsing,
package installation, or archive traversal are in scope. EverEye does not claim thermal
protection, universal Linux compatibility, or protection against a compromised desktop
session.
