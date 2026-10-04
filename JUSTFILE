set shell := ['bash', '-c']
set default-list := true

# Rebuilds the entire project
rebuild: Clean dsetup

[private]
Clean:
  rm -rf build
  rm -f compile_commands.json

# Setups up the development environment
dsetup:
  @echo '------------------------------------------------------------------------'
  @echo 'Setting up build directory, this build will be compiled in debug mode...'
  @echo '------------------------------------------------------------------------'

  cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DENIGMADB_BUILD_TOOLS=ON

  @echo '-------------------------------------------------'
  @echo 'Building the project this could take some time...'
  @echo '-------------------------------------------------'

  cmake --build build --parallel
  ln -sf build/compile_commands.json compile_commands.json

  @echo '--------------------------------------------------'
  @echo 'Build complete its a good idea to run just test ;)'
  @echo '--------------------------------------------------'

# Recompile the project
rc:
  cmake --build build --parallel

# Runs all the unit tests
test:
  @if [ -x './build/tests/unit_tests' ]; then \
    echo "Running tests..."; \
    ./build/tests/unit_tests; \
  else \
    echo "Skipping tests: './build/tests/unit_tests' does not exist"; \
  fi
