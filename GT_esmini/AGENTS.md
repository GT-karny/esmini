# GT_esmini extension guidance

- Work in this directory for custom behavior; do not move extension logic into `EnvironmentSimulator/` or `OSMP_FMU/`.
- Keep module dependencies directional: `io` and `scenario` depend on `core`; `control` depends on `core`, `io`, and `scenario`; `osi` depends on `core` and `scenario`.
- Use `<gt_esmini/...>` for public includes and `"gt_esmini/..."` for internal includes.
- Before changing controller, scenario, OSI, or web behavior, read `GT_esmini/CLAUDE.md` for the authoritative component map and runtime configuration locations.
