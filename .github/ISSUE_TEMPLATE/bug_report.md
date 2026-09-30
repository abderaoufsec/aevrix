---
name: Bug report
about: Report a reproducible problem with Aevrix
title: "fix: "
labels: bug
assignees: ""
---

## Description

A clear, concise description of the bug.

## Version / Environment

- Aevrix version or commit SHA:
- Build mode (Debug/Release) and TLS (`ENABLE_TLS=ON/OFF`):
- OS / compiler (`g++ --version`, `cmake --version`):
- OpenSSL version (if TLS involved):

## Configuration (redacted)

Paste the relevant `aevrix.conf` keys. **Strip `admin_token`, key paths, and secrets.**

```ini
host =
port =
# ...
```

## Steps to Reproduce

1.
2.
3.

## Expected Behaviour

What you expected to happen.

## Actual Behaviour

What happened instead (status codes, logs, crashes). Paste logs with `--log-level debug` if possible, redacted.

## Checklist

- [ ] I reproduced this on the latest `main` or a tagged release.
- [ ] I redacted tokens, keys, and secrets.
- [ ] I included a minimal repro (config + request).
