set shell := ["bash", "-c"]

default:
  @just --list

build:
  make compile NATIVE=1

test:
  make test NATIVE=1

# Install Conan dependencies (tsan profile)
# Removes the Conan-generated CMakePresets.json from the tsan output folder to prevent
# a duplicate "conan-debug" preset collision with the debug build in CMakeUserPresets.json.
deps-tsan:
    conan install . \
        --output-folder=build/tsan \
        --profile=conan/profiles/tsan \
        --build=missing
    rm -f build/tsan/CMakePresets.json
    python3 -c "\
import json, pathlib; \
p = pathlib.Path('CMakeUserPresets.json'); \
d = json.loads(p.read_text()); \
d['include'] = [x for x in d.get('include', []) if x != 'build/tsan/CMakePresets.json']; \
p.write_text(json.dumps(d, indent=4) + '\n')"

# Build with ThreadSanitizer
build-tsan: deps-tsan
    cmake --preset tsan
    cmake --build --preset tsan

# Run tests under ThreadSanitizer
test-tsan: build-tsan
    ctest --preset tsan --output-on-failure


lint:
  make lint NATIVE=1

format:
  make format NATIVE=1

format-check:
  make format.check NATIVE=1

clean:
  make clean

start:
  make docker.up

status:
  docker-compose ps

benchmark:
  make benchmark NATIVE=1

ci:
  make ci NATIVE=1
