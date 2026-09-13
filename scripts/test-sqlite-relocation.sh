#!/usr/bin/env bash
# Regression test for GH issue #60: exported CMake targets must keep
# depending on SQLite::SQLite3 instead of an absolute build-machine SQLite
# library path, so installed consumers keep working after the SQLite
# installation they were built against is moved or replaced.
#
# Usage: scripts/test-sqlite-relocation.sh [static|shared]
# Without an argument both linkage variants are tested.
set -euo pipefail

SQLITE_AMALGAMATION_URL="https://www.sqlite.org/2026/sqlite-amalgamation-3510200.zip"
SQLITE_AMALGAMATION_SHA3_256="9a9dd4eef7a97809bfacd84a7db5080a5c0eff7aaf1fc1aca20a6dc9a0c26f96"

VSQLITEPP_SOURCE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORK_DIR="${VSQLITE_RELOC_WORKDIR:-$(mktemp -d "${TMPDIR:-/tmp}/vsqlite-relocation.XXXXXX")}"

usage() {
  echo "usage: $0 [static|shared]" >&2
  exit 2
}

fail() {
  echo "FAIL: $*" >&2
  echo "work directory kept at ${WORK_DIR}" >&2
  exit 1
}

linkages=("$@")
if [ "${#linkages[@]}" -eq 0 ]; then
  linkages=(static shared)
fi
for linkage in "${linkages[@]}"; do
  case "${linkage}" in
    static|shared) ;;
    *) usage ;;
  esac
done

cmake_generator=()
if command -v ninja >/dev/null 2>&1; then
  cmake_generator=(-G Ninja)
fi

mkdir -p "${WORK_DIR}/downloads"
amalgamation_zip="${WORK_DIR}/downloads/sqlite-amalgamation.zip"

if [ ! -f "${amalgamation_zip}" ]; then
  echo "==> Downloading SQLite amalgamation"
  curl -fsSL "${SQLITE_AMALGAMATION_URL}" -o "${amalgamation_zip}"
fi

echo "==> Verifying amalgamation hash"
cat > "${WORK_DIR}/downloads/verify-hash.cmake" <<'EOF'
if(NOT CMAKE_ARGC EQUAL 5)
  message(FATAL_ERROR "usage: cmake -P verify-hash.cmake <file> <sha3-256>")
endif()
file(SHA3_256 "${CMAKE_ARGV3}" actual_hash)
if(NOT actual_hash STREQUAL CMAKE_ARGV4)
  message(FATAL_ERROR "hash mismatch: ${actual_hash}")
endif()
EOF
cmake -P "${WORK_DIR}/downloads/verify-hash.cmake" \
  "${amalgamation_zip}" "${SQLITE_AMALGAMATION_SHA3_256}"

echo "==> Extracting amalgamation"
unzip -qo "${amalgamation_zip}" -d "${WORK_DIR}/downloads"
amalgamation_dir="$(find "${WORK_DIR}/downloads" -maxdepth 1 -type d -name 'sqlite-amalgamation-*' | head -n1)"
[ -n "${amalgamation_dir}" ] || fail "amalgamation directory not found"

build_sdk() {
  local linkage="$1" sdk_dir="$2"
  mkdir -p "${sdk_dir}/include" "${sdk_dir}/lib"
  cp "${amalgamation_dir}/sqlite3.h" "${amalgamation_dir}/sqlite3ext.h" "${sdk_dir}/include/"

  local cc="${CC:-cc}"
  if [ "${linkage}" = "static" ]; then
    "${cc}" -O1 -c "${amalgamation_dir}/sqlite3.c" -o "${sdk_dir}/lib/sqlite3.o"
    ar rcs "${sdk_dir}/lib/libsqlite3.a" "${sdk_dir}/lib/sqlite3.o"
    rm "${sdk_dir}/lib/sqlite3.o"
  else
    "${cc}" -O1 -fPIC -shared -Wl,-soname,libsqlite3.so.0 \
      "${amalgamation_dir}/sqlite3.c" -o "${sdk_dir}/lib/libsqlite3.so" -lpthread -ldl -lm
    # Dependents record NEEDED libsqlite3.so.0 from the SONAME; provide the
    # matching symlink so the runtime loader resolves it inside the SDK
    # instead of falling back to the system SQLite.
    ln -s libsqlite3.so "${sdk_dir}/lib/libsqlite3.so.0"
  fi
}

install_targets_file() {
  local install_prefix="$1"
  local targets_file
  targets_file="$(find "${install_prefix}" -name vsqliteppTargets.cmake | head -n1)"
  [ -n "${targets_file}" ] || fail "vsqliteppTargets.cmake not found under ${install_prefix}"
  echo "${targets_file}"
}

run_linkage() {
  local linkage="$1"
  local build_dir="${WORK_DIR}/vsqlite-build-${linkage}"
  local consumer_build_dir="${WORK_DIR}/consumer-build-${linkage}"
  local install_prefix="${WORK_DIR}/install"
  local sdk_a="${WORK_DIR}/sdk-a"
  local sdk_b="${WORK_DIR}/sdk-b"
  local shared_libs=ON
  [ "${linkage}" = "static" ] && shared_libs=OFF

  echo "==> [${linkage}] Building external SQLite SDK at ${sdk_a}"
  rm -rf "${sdk_a}" "${sdk_b}"
  build_sdk "${linkage}" "${sdk_a}"

  echo "==> [${linkage}] Building and installing VSQLite++ against the SDK"
  cmake -S "${VSQLITEPP_SOURCE_DIR}" -B "${build_dir}" "${cmake_generator[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS="${shared_libs}" \
    -DVSQLITE_BUNDLED_SQLITE=OFF \
    -DVSQLITE_BUILD_EXAMPLES=OFF \
    -DVSQLITE_BUILD_TESTS=OFF \
    -DCMAKE_PREFIX_PATH="${sdk_a}" \
    -DCMAKE_INSTALL_PREFIX="${install_prefix}"
  cmake --build "${build_dir}"
  cmake --install "${build_dir}"

  local targets_file
  targets_file="$(install_targets_file "${install_prefix}")"

  echo "==> [${linkage}] Asserting exported targets reference the SQLite dependency target"
  # Either spelling is valid: the export resolves the alias to the target name
  # provided by the CMake that built VSQLite++.
  grep -Eq 'SQLite3::SQLite3|SQLite::SQLite3' "${targets_file}" \
    || fail "exported targets do not reference the SQLite dependency target"

  echo "==> [${linkage}] Asserting exported targets keep no absolute SDK path"
  if grep -Fq "${sdk_a}" "${targets_file}"; then
    fail "exported targets contain the build-machine path ${sdk_a}"
  fi

  echo "==> [${linkage}] Relocating the SDK to ${sdk_b}"
  mv "${sdk_a}" "${sdk_b}"

  echo "==> [${linkage}] Building installed consumer against the relocated SDK"
  cmake -S "${VSQLITEPP_SOURCE_DIR}/tests/package_consumers/install" \
    -B "${consumer_build_dir}" "${cmake_generator[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${install_prefix};${sdk_b}"
  cmake --build "${consumer_build_dir}"

  if [ "${linkage}" = "shared" ] && command -v ldd >/dev/null 2>&1; then
    echo "==> [${linkage}] Asserting the consumer resolves SQLite inside the relocated SDK"
    ldd "${consumer_build_dir}/vsqlitepp_install_consumer" | grep -Fq "${sdk_b}/lib" \
      || fail "consumer does not load SQLite from the relocated SDK"
  fi

  "${consumer_build_dir}/vsqlitepp_install_consumer"

  echo "==> [${linkage}] OK"
}

for linkage in "${linkages[@]}"; do
  run_linkage "${linkage}"
done

if [ -z "${VSQLITE_RELOC_WORKDIR:-}" ]; then
  rm -rf "${WORK_DIR}"
fi
echo "PASS: exported targets survive SQLite relocation (${linkages[*]})"
