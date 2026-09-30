## Description

What does this PR change and why? Link related issues (`Fixes #...`).

## Type of Change

- [ ] Bug fix
- [ ] Feature
- [ ] Documentation
- [ ] Test / CI
- [ ] Refactor / chore

## Testing

Commands run and results (paste ctest/smoke summary):

```text
ctest --test-dir build --output-on-failure
...
bash tests/smoke/run_all.sh
...
```

- [ ] `ctest` green (TLS-ON; TLS-OFF if TLS-gated code touched)
- [ ] Smoke suite green (if runtime behaviour changed)
- [ ] Sanitizer run (asan/ubsan/tsan) for parser/network/threading changes

## Documentation

- [ ] `README.md` / `docs/*.md` updated (if user-visible)
- [ ] `CHANGELOG.md` entry under `[Unreleased]`
- [ ] `aevrix.conf` sample updated (if config keys changed)

## Breaking Changes

None / describe migration:

## Security Considerations

Parser/TLS/proxy/WebSocket/path/auth impact, negative tests added:

## Checklist

- [ ] Small, single-purpose PR; refactors separated from fixes
- [ ] `.clang-format` clean, no new warnings (`-Werror`)
- [ ] No secrets, keys, or tokens committed
- [ ] I read [CONTRIBUTING.md](../CONTRIBUTING.md) and [SECURITY.md](../SECURITY.md)
