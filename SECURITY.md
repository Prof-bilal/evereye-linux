# Security policy

EverEye is a local Linux utility. It keeps session state in the user's state directory, communicates with power services through the user's D-Bus session and system logind/UPower interfaces, and does not require a root helper.

## Reporting a vulnerability

Please report security issues privately through the repository's security reporting channel. Include the EverEye version, distribution, desktop session, architecture, exact reproduction steps, and any relevant logs with personal paths or identifiers removed. Do not publish an exploitable proof of concept in a public issue before a fix is available.

If the hosting repository does not expose a private reporting channel, open a minimal public issue requesting a private contact and omit secrets, personal data, and weaponized details.

## Scope

Reports involving session ownership, D-Bus authorization, inhibitor release, journal permissions, watchdog targeting, package installation, or archive traversal are in scope. EverEye does not claim thermal protection, universal Linux compatibility, or protection against a compromised desktop session.
