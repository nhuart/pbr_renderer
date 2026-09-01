.PHONY: help configure-debug configure-release build-debug build-release run-debug run-release format lint

help:
	@echo "Available commands:"
	@echo "  make build-debug      - Build with validation layers (debug)"
	@echo "  make build-release    - Build optimized without validation layers"
	@echo "  make run-debug SCENE=<path>   - Run debug (e.g. SCENE=scenes/scene_a.json)"
	@echo "  make run-release SCENE=<path> - Run release"
	@echo "  make format           - Format all source files with clang-format"
	@echo "  make lint             - Run clang-tidy static analysis"
	@echo "  make lint-fix         - Run clang-tidy and apply fixes automatically"

.DEFAULT_GOAL := help

configure-debug:
	cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

configure-release:
	cmake -B build/release -DCMAKE_BUILD_TYPE=Release

build-debug:
	cmake --build build/debug -- --no-print-directory

build-release:
	cmake --build build/release -- --no-print-directory

run-debug:
	./build/debug/pbr_renderer $(SCENE)

run-release:
	./build/release/pbr_renderer $(SCENE)

format:
	find . -path ./build -prune -o \( -name "*.cpp" -o -name "*.h" \) -print | xargs clang-format -i

lint:
	run-clang-tidy -p build/debug -quiet -j$(shell nproc) "^$(CURDIR)/[^/]+\.cpp"

lint-fix:
	run-clang-tidy -p build/debug -quiet -fix -j$(shell nproc) "^$(CURDIR)/[^/]+\.cpp"