# Roblox Icon Fix

A small Windows utility for missing Roblox game thumbnails and avatars caused by DNS or connectivity issues with `tr.rbxcdn.com`.

**Windows 10 1803+ / Windows 11, x64. Russian UI. MIT license.**

## Download and use

Download `RobloxIconFix.exe` from this repository's **Releases** section.

1. Completely close Roblox.
2. Run the executable and approve the administrator prompt.
3. Click **Исправить иконки** (Fix icons).
4. Wait for the result, then restart Roblox.

**Проверить** runs diagnostics without changing hosts. **Отменить фикс** rolls back this utility's changes. **Открыть журнал** opens logs and backups.

## How it works

The application queries Google and Cloudflare over HTTPS, tests candidate CDN addresses with TLS verification enabled, and updates the hosts entry for `tr.rbxcdn.com`. When Roblox's public thumbnail API is reachable, it also checks an actual image response. Otherwise, it explicitly reports that only HTTPS connectivity was verified.

A full hosts backup is created before a change. Existing mappings of the target domain are preserved in reversible comments. Unrelated entries are preserved. A failed post-apply check triggers rollback unless another application has modified the file in the meantime.

No Roblox credentials are required. No drivers, services or startup entries are installed. The application uses Windows' built-in `curl.exe` and does not require Python or .NET. The executable is unsigned.

## Limits and validation

This is a targeted CDN address fix. It cannot guarantee access when every candidate endpoint is blocked, Roblox is experiencing an outage, or other domains are affected. A custom proxy or browser DNS configuration can affect the result.

One user reported successful image restoration with version 1.0. Portable core tests have passed. Windows CI is configured but had not run when this package was prepared. See [validation details](../VALIDATION.md).

## Build

Use `build_msvc.bat` in an **x64 Native Tools Command Prompt**, or run `bash build_mingw.sh` with MinGW-w64. The output is `build/RobloxIconFix.exe`. GitHub Actions is configured to run core tests on Linux and Windows and build an unsigned Windows executable.

See the [main README](../README.md) and [technical documentation in Russian](../README_RU.md). Independent project, not affiliated with Roblox Corporation.
