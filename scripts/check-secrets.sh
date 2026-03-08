#!/usr/bin/env bash
set -euo pipefail

mapfile -t staged < <(git diff --cached --name-only --diff-filter=ACMR)
if ((${#staged[@]} == 0)); then
  exit 0
fi

if printf '%s\n' "${staged[@]}" | rg -n '(^|/)\.env(\..*)?$' >/dev/null; then
  echo "ERROR: .env files cannot be committed." >&2
  printf '%s\n' "${staged[@]}" | rg -n '(^|/)\.env(\..*)?$' >&2 || true
  exit 1
fi

added_lines="$(
  git diff --cached -U0 -- "${staged[@]}" \
    | rg -v '^\+\+\+ ' \
    | rg '^\+' \
    | sed 's/^+//'
)"

if [[ -z "${added_lines}" ]]; then
  exit 0
fi

pattern='(?i)(-----BEGIN (RSA|EC|OPENSSH|DSA|PGP) PRIVATE KEY-----|ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|AKIA[0-9A-Z]{16}|ASIA[0-9A-Z]{16}|authorization\s*:\s*(bearer|basic)\s+[A-Za-z0-9._~+/-]{16,}|(password|passwd|token|secret|api[_-]?key)\s*[:=]\s*["'"'"'][^"'"'"']{8,}["'"'"'])'

if printf '%s\n' "$added_lines" | rg -n -P "$pattern" >/tmp/wiiuromm_secret_hits.txt; then
  echo "ERROR: potential secrets detected in staged changes:" >&2
  cat /tmp/wiiuromm_secret_hits.txt >&2
  rm -f /tmp/wiiuromm_secret_hits.txt
  exit 1
fi

rm -f /tmp/wiiuromm_secret_hits.txt
exit 0
