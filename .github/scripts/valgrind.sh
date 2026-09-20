#!/usr/bin/env bash

set -euo pipefail

readonly REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly SCRIPT_PATH="$REPO_DIR/.github/scripts/valgrind.sh"
# Keep this distinct from the compiler and test harnesses' ordinary statuses.
readonly MEMCHECK_ERROR_EXIT_CODE=97

# Run under memcheck.
run_under_memcheck() {
  if [ "$#" -eq 0 ]; then
    echo "Compiler Valgrind: --run requires an executable." >&2
    return 2
  fi
  if [ -z "${DIOPTASE_VALGRIND_LOG_DIR:-}" ]; then
    echo "Compiler Valgrind: DIOPTASE_VALGRIND_LOG_DIR is not set." >&2
    return 2
  fi

  local log
  local status
  log="$(mktemp "$DIOPTASE_VALGRIND_LOG_DIR/memcheck.XXXXXX.log")"
  printf '%q ' "$@" > "$log.command"
  printf '\n' >> "$log.command"

  set +e
  "${VALGRIND:-valgrind}" \
    --tool=memcheck \
    --quiet \
    --leak-check=full \
    --show-leak-kinds=all \
    --errors-for-leak-kinds=all \
    --track-origins=yes \
    --error-exitcode="$MEMCHECK_ERROR_EXIT_CODE" \
    --log-file="$log" \
    "$@"
  status=$?
  set -e

  # Invalid-input tests expect bcc to return nonzero. Record Memcheck diagnostics
  # separately so those tests cannot mistake a memory error for success.
  # Fork-isolated harness children can report an error while their parent later
  # returns its generic failure status, so a nonempty quiet log is also a finding.
  if [ "$status" -eq "$MEMCHECK_ERROR_EXIT_CODE" ] || [ -s "$log" ]; then
    touch "$log.finding"
  fi
  return "$status"
}

# The emulator execution harnesses require DIOPTASE_BCC to name one executable,
# rather than a command prefix. The main mode creates this symlink so child bcc
# processes use the same Memcheck wrapper as direct compiler invocations.
if [ "${0##*/}" = "bcc-valgrind" ]; then
  if [ -z "${DIOPTASE_VALGRIND_BCC:-}" ]; then
    echo "Compiler Valgrind: DIOPTASE_VALGRIND_BCC is not set." >&2
    exit 2
  fi
  run_under_memcheck "$DIOPTASE_VALGRIND_BCC" "$@"
  exit $?
fi

if [ "${1:-}" = "--run" ]; then
  shift
  run_under_memcheck "$@"
  exit $?
fi

if [ "$#" -ne 0 ]; then
  echo "Usage: $0" >&2
  exit 2
fi

valgrind_bin="${VALGRIND:-valgrind}"
if ! command -v "$valgrind_bin" >/dev/null 2>&1; then
  echo "Compiler Valgrind: '$valgrind_bin' is required; install Valgrind or set VALGRIND to its path." >&2
  exit 1
fi
if ! "$valgrind_bin" --version >/dev/null 2>&1; then
  echo "Compiler Valgrind: '$valgrind_bin --version' failed." >&2
  exit 1
fi

work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
export DIOPTASE_VALGRIND_LOG_DIR="$work_dir"
export DIOPTASE_VALGRIND_BCC="$REPO_DIR/build/debug/bcc"
export VALGRIND="$valgrind_bin"

bcc_wrapper="$work_dir/bcc-valgrind"
ln -s "$SCRIPT_PATH" "$bcc_wrapper"
suite_log="$work_dir/suite.log"
runner="$SCRIPT_PATH --run"
set +e
make -C "$REPO_DIR" test \
  TEST_EXEC="$runner $REPO_DIR/build/debug/bcc" \
  TAC_INTERP_TEST_EXEC="$runner $REPO_DIR/build/debug/tac_interpreter_tests" \
  TAC_EXEC_TEST_EXEC="$runner $REPO_DIR/build/debug/tac_exec_tests" \
  EMU_EXEC_TEST_EXEC="$runner $REPO_DIR/build/debug/emu_exec_tests" \
  EMU_EXEC_FULL_TEST_EXEC="$runner $REPO_DIR/build/debug/emu_exec_full_tests" \
  EMU_EXEC_BCC="$bcc_wrapper" \
  EMU_EXEC_FULL_BCC="$bcc_wrapper" \
  2>&1 | tee "$suite_log"
make_status=${PIPESTATUS[0]}
set -e

failed=0
if [ "$make_status" -ne 0 ]; then
  echo "Compiler Valgrind: make test exited with status $make_status." >&2
  failed=1
fi

summary="$(grep -E '^Summary: [0-9]+ / [0-9]+ tests passed\.$' "$suite_log" | tail -n 1 || true)"
if [[ ! "$summary" =~ ^Summary:\ ([0-9]+)\ /\ ([0-9]+)\ tests\ passed\.$ ]]; then
  echo "Compiler Valgrind: expected an aggregate test summary, observed '${summary:-none}'." >&2
  failed=1
elif [ "${BASH_REMATCH[1]}" != "${BASH_REMATCH[2]}" ]; then
  echo "Compiler Valgrind: functional tests failed under Memcheck: $summary" >&2
  failed=1
fi

shopt -s nullglob
findings=("$work_dir"/*.log.finding)
if [ "${#findings[@]}" -ne 0 ]; then
  echo "Compiler Valgrind: Memcheck reported ${#findings[@]} failing invocation(s)." >&2
  for marker in "${findings[@]}"; do
    log="${marker%.finding}"
    echo "--- ${log##*/} ---" >&2
    echo "  Command: $(cat "$log.command")" >&2
    sed 's/^/  /' "$log" >&2
  done
  failed=1
fi

if [ "$failed" -ne 0 ]; then
  exit 1
fi
echo "Compiler Valgrind: all debug-suite invocations passed Memcheck."
