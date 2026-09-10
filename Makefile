.PHONY: help configure-debug configure-release build-debug build-release run-debug run-release format lint \
	screenshot-all \
	screenshot-stanford-bunny-pbr screenshot-stanford-bunny-pbr-spot screenshot-stanford-bunny-pbr-point screenshot-stanford-bunny-phong screenshot-stanford-bunny-blinn-phong screenshot-stanford-bunny-lambertian \
	screenshot-viking-room screenshot-viking-room-particles

help:
	@echo "Available commands:"
	@echo "  make build-debug      - Build with validation layers (debug)"
	@echo "  make build-release    - Build optimized without validation layers"
	@echo "  make run-debug SCENE=<path>   - Run debug (e.g. SCENE=scenes/scene_a.json)"
	@echo "  make run-release SCENE=<path> - Run release"
	@echo "  make format           - Format all source files with clang-format"
	@echo "  make lint             - Run clang-tidy static analysis"
	@echo "  make lint-fix         - Run clang-tidy and apply fixes automatically"
	@echo "  make screenshot-all   - Capture screenshots for all scenes"
	@echo "  make screenshot-stanford-bunny-pbr        - Stanford bunny PBR (directional light)"
	@echo "  make screenshot-stanford-bunny-pbr-spot   - Stanford bunny PBR (spot light)"
	@echo "  make screenshot-stanford-bunny-pbr-point  - Stanford bunny PBR (point light)"
	@echo "  make screenshot-stanford-bunny-phong      - Stanford bunny Phong"
	@echo "  make screenshot-stanford-bunny-blinn-phong - Stanford bunny Blinn-Phong"
	@echo "  make screenshot-stanford-bunny-lambertian  - Stanford bunny Lambertian"
	@echo "  make screenshot-viking-room               - Viking room (single)"
	@echo "  make screenshot-viking-room-particles     - Three viking rooms with particles"

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
	./build/debug/pbr_renderer $(SCENE) $(ARGS)

run-release:
	./build/release/pbr_renderer $(SCENE) $(ARGS)

format:
	find . -path ./build -prune -o \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) -print | xargs clang-format -i

lint:
	run-clang-tidy -p build/debug -quiet -j$(shell nproc) "^$(CURDIR)/src/.*\.cpp$$"

lint-fix:
	run-clang-tidy -p build/debug -quiet -fix -j$(shell nproc) "^$(CURDIR)/src/.*\.cpp$$"

screenshot-stanford-bunny-pbr:
	$(MAKE) run-debug SCENE=scenes/stanford_bunny_pbr.json ARGS='--screenshot results/standford_bunny/pbr.png'

screenshot-stanford-bunny-pbr-spot:
	$(MAKE) run-debug SCENE=scenes/stanford_bunny_pbr_spot.json ARGS='--screenshot results/standford_bunny/pbr_spot.png'

screenshot-stanford-bunny-pbr-point:
	$(MAKE) run-debug SCENE=scenes/stanford_bunny_pbr_point.json ARGS='--screenshot results/standford_bunny/pbr_point.png'

screenshot-stanford-bunny-phong:
	$(MAKE) run-debug SCENE=scenes/stanford_bunny_phong.json ARGS='--screenshot results/standford_bunny/phong.png'

screenshot-stanford-bunny-blinn-phong:
	$(MAKE) run-debug SCENE=scenes/stanford_bunny_blinn_phong.json ARGS='--screenshot results/standford_bunny/blinn_phong.png'

screenshot-stanford-bunny-lambertian:
	$(MAKE) run-debug SCENE=scenes/stanford_bunny_lambertian.json ARGS='--screenshot results/standford_bunny/lambertian.png'

screenshot-viking-room:
	$(MAKE) run-debug SCENE=scenes/viking_room.json ARGS='--screenshot results/viking_room/single.png'

screenshot-viking-room-particles:
	$(MAKE) run-debug SCENE=scenes/three_viking_rooms_particles.json ARGS='--screenshot results/viking_room/three_rooms.png'

screenshot-all: screenshot-stanford-bunny-pbr screenshot-stanford-bunny-pbr-spot screenshot-stanford-bunny-pbr-point screenshot-stanford-bunny-phong screenshot-stanford-bunny-blinn-phong screenshot-stanford-bunny-lambertian screenshot-viking-room screenshot-viking-room-particles