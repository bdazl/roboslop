# Roboslop top-level Makefile. All commands route through CMakePresets.
#
# Usage:
#   make prepare PRESET=debug           bootstrap, then configure for PRESET
#   make bootstrap PRESET=debug         conan install for PRESET
#   make configure                      cmake --preset
#   make build                          cmake --build --preset (everything)
#   make test                           ctest --preset
#   make shaders                        compile shaders for PRESET
#   make format / format-check          clang-format
#   make tidy                           clang-tidy via compile_commands.json
#   make compdb                         refresh /compile_commands.json symlink
#   make clean                          rm -rf build/$(PRESET)
#   make distclean                      rm -rf build/
#   make all                            bootstrap configure build
#
# Per-app development loop (apps are discovered from apps/*/):
#   make apps                           list the apps
#   make <app> [ARGS="..."]             build only that app (and what it
#                                       needs), then run it — the inner loop
#   make build-<app>                    build only that app
#   make run-<app> [ARGS="..."]         run it without building
#   make run [APP=gorden] [ARGS="..."]  same as run-$(APP)
#
# Every target honours PRESET (debug by default — the development flavour:
# asserts, bgfx debug output, no optimisation). Use PRESET=release for
# performance runs, PRESET=asan-ubsan / tsan for sanitised runs.

PRESET ?= debug
APP    ?= gorden
JOBS   ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# One directory per app under apps/; each builds an executable named after
# its directory at build/<preset>/apps/<app>/<app>.
APPS := $(sort $(notdir $(wildcard apps/*)))

.DEFAULT_GOAL := help

.PHONY: prepare bootstrap configure build test run shaders apps \
        format format-check tidy compdb \
        clean distclean all help \
        $(APPS) $(addprefix build-,$(APPS)) $(addprefix run-,$(APPS))

# Recursive recipes keep configure after bootstrap, including with make -j.
prepare:
	@$(MAKE) --no-print-directory bootstrap
	@$(MAKE) --no-print-directory configure

bootstrap:
	@./scripts/bootstrap.sh $(PRESET)

configure:
	@cmake --preset $(PRESET)
	@./scripts/compdb.sh $(PRESET)

build:
	@cmake --build --preset $(PRESET) -j$(JOBS)

test:
	@ctest --preset $(PRESET)

run: run-$(APP)

# --- Per-app targets ------------------------------------------------------

apps:
	@printf '%s\n' $(APPS)

# Static pattern rules rather than plain `build-%` patterns: implicit
# rules are never searched for .PHONY targets, and these must be phony.

# Build one app's CMake target. Its shaders and the engine library come
# along through CMake dependencies; other apps and the tests do not.
$(addprefix build-,$(APPS)): build-%:
	@cmake --build --preset $(PRESET) --target $* -j$(JOBS)

# Run from build/<preset> so the relative assetRoot ("assets") resolves.
$(addprefix run-,$(APPS)): run-%:
	@cd build/$(PRESET) && exec apps/$*/$* $(if $(filter gorden,$*),--dev) $(ARGS)

# `make shaderlab`: the edit → build → run loop for a single app.
$(APPS): %: build-%
	@$(MAKE) --no-print-directory run-$@ ARGS='$(ARGS)'

shaders:
	@cmake --build --preset $(PRESET) --target shaders -j$(JOBS)

format:
	@./scripts/format.sh

format-check:
	@./scripts/format.sh --check

tidy:
	@./scripts/tidy.sh

compdb:
	@./scripts/compdb.sh $(PRESET)

clean:
	@rm -rf build/$(PRESET)

distclean:
	@rm -rf build/

all: bootstrap configure build

help:
	@printf 'Roboslop Makefile targets (PRESET=%s):\n' '$(PRESET)'
	@printf '  prepare         bootstrap, then configure for $(PRESET)\n'
	@printf '  bootstrap       conan install for PRESET (writes toolchain file)\n'
	@printf '  configure       cmake --preset $(PRESET)\n'
	@printf '  build           cmake --build --preset $(PRESET) -j$(JOBS)\n'
	@printf '  test            ctest --preset $(PRESET)\n'
	@printf '  shaders         compile shaders for $(PRESET)\n'
	@printf '  apps            list apps: %s\n' '$(APPS)'
	@printf '  <app>           build only <app>, then run it (e.g. make shaderlab)\n'
	@printf '  build-<app>     build only <app>\n'
	@printf '  run-<app>       run build/$(PRESET)/apps/<app>/<app> without building\n'
	@printf '  run             run-$$(APP), APP=%s; ARGS="..." is forwarded\n' '$(APP)'
	@printf '  format          clang-format -i across the source tree\n'
	@printf '  format-check    clang-format --dry-run (CI)\n'
	@printf '  tidy            clang-tidy via compile_commands.json\n'
	@printf '  compdb          refresh /compile_commands.json symlink\n'
	@printf '  clean           rm -rf build/$(PRESET)\n'
	@printf '  distclean       rm -rf build/\n'
	@printf '  all             bootstrap configure build\n'
