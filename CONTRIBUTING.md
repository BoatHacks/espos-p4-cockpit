# Contributing

Bug reports, feature requests and pull requests are welcome at
<https://github.com/dirkwa/espos-p4-cockpit>.

Before opening a pull request, make sure the firmware still builds:

```bash
scripts/build.sh
```

That wrapper is a nice'd `idf.py build` that holds a lock so two builds
never race on `build/` — on a small machine a bare `idf.py build`
saturates every core and the editor/SSH session stops being scheduled.

Follow the conventions in [AGENTS.md](AGENTS.md): Angular-style commit and PR
titles, and never commit local or boat configuration.

## Contributor license grant

By submitting a pull request or patch, you grant Dirk Wahrheit a perpetual,
worldwide, non-exclusive, royalty-free, irrevocable license to use, reproduce,
modify, publish, sublicense and distribute your contribution, and to relicense
it under any terms, including as part of espos-p4-cockpit releases. You confirm
that you have the right to grant this.
