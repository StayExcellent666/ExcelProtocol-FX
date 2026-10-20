# Updates and verification

The Updates tab in the ExcelProtocol FX dock contains:

- **Automatically Check for Updates** (enabled initially): checks 20 seconds after OBS starts, then every 24 hours while OBS is open. Manual checking remains available when this is disabled.
- **Automatically Install Updates** (disabled initially): downloads a compatible update and queues it for installation after OBS closes. Enabling this option authorizes downloads and deferred installation; Windows may still require administrator approval after OBS exits.
- **Stable releases** (default) and **Stable and beta releases** channels.
- Manual check, download, and cancel buttons. A manual download also queues installation after OBS closes.

Selecting another channel cancels any download or queued update. Disabling automatic installation cancels a pending automatic update. Updates are never installed while an OBS process is running. The plugin and helper never terminate or restart OBS. An installer failure, another OBS instance, or declined UAC approval leaves the update uninstalled; the next launch displays the helper result when available. A queued update can be cancelled before exiting OBS.

Settings are per Windows user under `HKCU\Software\ExcelProtocol\ExcelProtocol-FX`. Staging uses `%LOCALAPPDATA%\ExcelProtocol\ExcelProtocol-FX\updates\<random-id>` (the Windows Qt generic data location). Each attempted installation retains `result.txt` and `installer.log` for diagnosis. The installer and copied helper also remain there after an attempt; these old update directories can be removed manually after all update helpers and OBS instances have closed.

## Repository and channel policy

The repository is compiled into the plugin as `FX_GITHUB_REPOSITORY`; the user interface and downloaded metadata cannot override it. Builds without a valid official repository disable network updates. Stable checks use `https://api.github.com/repos/<owner>/ExcelProtocol-FX/releases/latest`. The beta channel considers the newest compatible version among the first 30 published releases returned by `/releases?per_page=30`; it accepts stable versions and `vMAJOR.MINOR.PATCH-beta.NUMBER` prereleases. Drafts, inconsistent prerelease flags, other prerelease schemes, malformed versions, equal versions and downgrades are rejected. A release with a newer version but incompatible OBS bounds is reported as incompatible and is not installed.

The request identifies the plugin version in its User-Agent. No settings, scenes, workbook information, credentials or personal telemetry are sent. GitHub sees the normal network request, including its source IP. No GitHub token is required for public release downloads; unauthenticated API rate limits may temporarily prevent checking.

## Verification boundary

The updater requires all of the following before queueing setup:

1. A release from the exact compiled repository with the expected tag and release URL.
2. Exactly one correctly named Windows x64 installer and one `update-manifest.json` asset, each with GitHub's `sha256:` asset digest, size and uploaded state.
3. An HTTPS download with normal TLS certificate validation. API redirects are rejected; asset redirects are restricted to the GitHub release asset hosts. At most four redirects are followed. Cookies and credential reuse are disabled.
4. A manifest matching the GitHub release identity, channel, platform, installer name, size, URL and SHA-256 digest, with compatible OBS version bounds.
5. Streamed installer verification of both byte count and SHA-256 before its atomic staging file is committed. Metadata requests and downloads have time and size limits (4 MiB API response, 1 MiB manifest, 250 MiB installer).
6. A second SHA-256 check in the helper after the launching OBS process exits. The helper keeps the installer open without write or delete sharing until installation finishes, preventing replacement after verification.

These checks establish transport and release-asset integrity through the trusted GitHub repository. **They are not an independent publisher signature.** The release owner or a compromised publishing credential can publish new matching hashes; SHA-256 does not protect against that. This project does not claim Authenticode signing unless the actual release has separately been signed. Windows may show an unknown-publisher UAC prompt for unsigned setup.

## Deferred installer safety

On OBS exit the plugin starts a copy of the native helper, passing only an inherited handle to that exact OBS process, its PID, a local installer path, the expected digest and the canonical OBS root. The helper validates the handle's executable identity, waits at most 24 hours, then enumerates all processes. It fails closed if another `obs64.exe` or `obs32.exe` exists or enumeration fails. PID reuse cannot satisfy the inherited process handle. The helper rejects relative, UNC, alternate-stream and reparse-point paths.

Immediately before launching the verified setup it checks for OBS processes again. Setup receives fixed `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /DIR=... /LOG=...` arguments through the Windows API, never a shell or command from metadata. UAC is requested only at this point. The installer independently refuses running OBS and locked destinations and owns rollback of its payload if installation fails. There is no force-close, scheduled task, background service, startup entry or silent bypass of UAC. A user can still start OBS concurrently with setup; the process checks and file locking fail the update rather than requesting an OBS restart. Do not reopen OBS until setup completes.

## Verification tests

`update-policy-test` exercises SemVer ordering, stable/beta selection, draft rejection, repository and redirect restrictions, mandatory digests, asset size and duplicate handling, manifest agreement and OBS compatibility. `excelprotocol-fx-updater --self-test` validates argument/path rules without installing anything. `--verify-only <file> <sha256>` checks a local fixture without networking, process launch, UAC or installation. Production installation requires the inherited OBS process handle and all normal validations; the test modes cannot install.

End-to-end deferred installation and rollback should be exercised using the release checklist and an isolated OBS installation before a stable release is promoted. Unit tests alone do not establish successful installation or successful OBS loading.

References: [GitHub releases REST API](https://docs.github.com/en/rest/releases/releases), [Qt redirect policy](https://doc.qt.io/qt-6/qnetworkrequest.html#RedirectPolicy-enum), [Windows process handles](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject).
