## What

One or two sentences on what changed and why. Link issues (`Fixes #12`).

## Testing

Commands you ran and what they printed:

```text
ctest --test-dir build --output-on-failure
bash tests/smoke/run_all.sh
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

- [ ] CTest green, plus a `-DENABLE_TLS=OFF` build if TLS-gated code changed
- [ ] Smoke suite green if runtime behaviour changed
- [ ] Sanitizer run if the change touches memory, parsing or threads

## Notes

New or changed configuration keys, docs that needed updating, breaking changes,
and any security impact (framing, paths, limits, auth, origins).
