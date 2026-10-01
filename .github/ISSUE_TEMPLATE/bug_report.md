---
name: Bug report
about: A reproducible problem in Aevrix
title: "fix: "
labels: bug
---

## What happens

One paragraph: what you did, what you expected, what happened instead.

## Environment

- Aevrix version or commit SHA:
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug|Release`, `ENABLE_TLS=ON|OFF`:
- OS and compiler (`uname -a`, `g++ --version`):
- Command line (`./build/aevrix --config <file>`, or `--root <path>`):

## Configuration

The relevant keys from `aevrix.conf`, with `admin_token` and key paths removed:

```ini
host =
port =
document_root =
```

## Steps

1.
2.
3.

## Logs

Relevant lines with `log_level = debug`, secrets removed.

- [ ] Reproduced on the latest `main` or a tagged release
- [ ] Checked that it is not listed as a known limitation in `SECURITY.md` or `docs/HTTP.md`
