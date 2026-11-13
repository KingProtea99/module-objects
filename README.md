# Basic objects (MealKit module)

Trigger volumes, path platforms, timers, rule rows and item spawn points for [MealKit](https://github.com/MealKit-Engine/MealKit) games, deterministic and without physics. Docs: [docs/index.md](docs/index.md).

> **Beta (v0.x).** MealKit is under active development: APIs and file formats may change between versions until v1.0, and projects are moved forward with the upgrade tool. Feedback and bug reports are welcome.

```
tools\module.ps1 add -Project <your project> objects
```

Tests (not copied into projects): `cmake -S tests -B build/tests -G Ninja -DMEALKIT_ROOT=<engine> -DMEALKIT_HEADLESS_ONLY=ON`, then build and run `objects_tests`.

MIT license.
