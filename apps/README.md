# apps/ — one ESP-IDF component per app (L5)

Every `apps/app_<name>/` directory is an ESP-IDF component, discovered through
`EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt`. Adding an app is exactly
(ARCHITECTURE.md §11, ADR-0009, ADR-0016):

1. `apps/app_<name>/` with its sources and a `CMakeLists.txt` that
   `REQUIRES stark_app` (plus `stark_ui`, `stark_gfx`, … as it uses them) **and is
   registered `WHOLE_ARCHIVE`**, defining `const stark_app_t app_<name>`:

   ```cmake
   idf_component_register(SRCS "app_<name>.c" REQUIRES stark_app stark_ui stark_gfx WHOLE_ARCHIVE)
   ```

   `WHOLE_ARCHIVE` is what keeps step 1 self-contained: nothing requires an app
   component (that would be an upward L4 → L5 dependency), so ESP-IDF places its
   library before the registry in the link line and `main/app_registry.c`'s reference to
   `app_<name>` would be undefined. Whole-archive linking includes the app's objects
   regardless of order (verified in STARK-0019);
2. one `extern const stark_app_t app_<name>;` in `main/app_registry.h`;
3. one `&app_<name>,` line in the `stark_apps[]` array in `main/app_registry.c`.

The registry lives at the composition root (ADR-0016), so adding an app edits nothing
under `components/`. Nothing else. If a new app needs any other core edit, the abstraction is wrong.
