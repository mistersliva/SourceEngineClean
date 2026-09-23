# =============================================================================
# lint-legacy.ps1 - Phase 0 legacy-code ratchet lint for SourceEngineClean
#
# Counts known categories of legacy/deprecated/32-bit-only code using
# `git grep` (fast, tracked files only, skips submodules by default).
#
# The counts are compared against scripts/lint-baseline.json:
#   - count > baseline  -> FAIL (a check introduced new legacy code)
#   - count < baseline  -> WARN (improvement; re-run with -UpdateBaseline)
#   - count = baseline  -> OK
#
# Usage:
#   powershell -File scripts/lint.ps1                  # verify (CI mode)
#   powershell -File scripts/lint.ps1 -UpdateBaseline  # ratchet down after cleanup
#   powershell -File scripts/lint.ps1 -Json            # machine-readable report
#
# Notes:
#   - Patterns are POSIX ERE (no \s, \b, \w) so they work with `git grep -E`
#     on Windows, Linux and macOS alike.
#   - This script must run inside the git work tree.
# =============================================================================
param(
    [switch] $UpdateBaseline,
    [string] $BaselinePath = "",
    [switch] $Json
)

$ErrorActionPreference = 'Stop'

# --- locate repo root --------------------------------------------------------
$repoRoot = (git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $repoRoot) {
    Write-Error "lint-legacy.ps1 must be run inside the git work tree."
    exit 2
}
$repoRoot = $repoRoot.Trim()
if (-not $BaselinePath) { $BaselinePath = Join-Path $repoRoot "scripts\lint-baseline.json" }

# Source file pathspecs for git grep
$pathSpec = @('*.c', '*.cc', '*.cpp', '*.cxx', '*.h', '*.hh', '*.hpp', '*.inl')

# --- the checks --------------------------------------------------------------
# id          : stable identifier used in the baseline file
# pattern     : POSIX extended regex, matched with git grep -E
# why         : short rationale shown in reports
$checks = @(
    [pscustomobject]@{
        id = 'x360_refs'
        pattern = '_X360'
        why = 'Xbox 360 conditionals (Phase 1a: unwrap #else arms, delete)'
    },
    [pscustomobject]@{
        id = 'isx360_fn'
        pattern = 'IsX360[[:space:]]*\('
        why = 'Runtime Xbox 360 checks (Phase 1a)'
    },
    [pscustomobject]@{
        id = 'xbox_include'
        pattern = '#[[:space:]]*include[[:space:]]*[^[:space:]>]*xbox/'
        why = 'Includes of xbox/ headers & xboxstubs.h (Phase 1a)'
    },
    [pscustomobject]@{
        id = 'fn3dnow'
        pattern = 'MathLib_3DNowEnabled|_3DNow_|bAllow3DNow|b3DNow|Check3DNowTechnology|3dnow\.(cpp|h)|amd3dx\.h'
        why = '3DNow! math paths (Phase 1a: deleted with mathlib/3dnow)'
    },
    [pscustomobject]@{
        id = 'dx_to_gl_abstraction'
        pattern = 'DX_TO_GL_ABSTRACTION'
        why = 'Fake-D3D9-over-GL define (Phase 3: dies with togl/togles)'
    },
    [pscustomobject]@{
        id = 'win32_long_no_ptr'
        pattern = '(Get|Set)WindowLong([AW])?[[:space:]]*\('
        why = 'Get/SetWindowLong without Ptr (Phase 2a: truncates on x64). Whitespace-tolerant so a `Long (` spelling cannot hide a hit - the pattern must not match the fixed ...LongPtr form, which it cannot, since Ptr is neither [AW] nor whitespace.'
    },
    [pscustomobject]@{
        id = 'inline_asm'
        pattern = '__asm([^[:alnum:]_]|$)|_asm[[:space:]]*\{'
        why = 'MSVC inline assembly, cannot compile for x64 (Phase 2a)'
    },
    [pscustomobject]@{
        id = 'd3d9_com_types'
        pattern = 'IDirect3D(Device9|3D9|Surface9|Texture9|VertexBuffer9|IndexBuffer9)[^[:alnum:]_]'
        why = 'Direct3D 9 COM types (Phase 3: delete with DX9 backend)'
    },
    [pscustomobject]@{
        id = 'suspicious_ptr_cast'
        pattern = '\([[:space:]]*(unsigned[[:space:]]+int|int|signed[[:space:]]+int|long|unsigned[[:space:]]+long|DWORD|uint32|int32)[[:space:]]*\)[[:space:]]*(\([[:space:]]*(const[[:space:]]+)?(void|char|unsigned[[:space:]]+char|signed[[:space:]]+char|wchar_t|struct[[:space:]]+[[:alnum:]_]+|[[:alnum:]_:]+)[[:space:]]*\*+[[:space:]]*\)|&[[:space:]]*[[:alnum:]_])|reinterpret_cast<[[:space:]]*(int|unsigned([[:space:]]+int)?|long|DWORD|uint32|int32)[[:space:]]*>'
        why = 'Pointer -> 32-bit int casts (Phase 2a; compiler C4311/C4302 is the ultimate gate)'
    },
    [pscustomobject]@{
        id = 'local_minmax_macro'
        pattern = '#[[:space:]]*define[[:space:]]+(min|max|Max)([^[:alnum:]_]|$)'
        why = 'Local file-scope min/max macro wrappers (Phase 1a Stage 5; residue = valve_minmax_on.h + vendored my_global.h)'
    }
)

# --- counting ----------------------------------------------------------------
function Get-CheckCount([string] $pattern) {
    $out = & git grep -I -c -E -- $pattern -- $script:pathSpec 2>$null
    $code = $LASTEXITCODE
    if ($code -ge 2) { throw "git grep failed (exit $code) for pattern: $pattern" }
    if ($code -eq 1 -or $null -eq $out) { return 0 }
    $total = 0
    foreach ($line in @($out)) {
        $idx = $line.ToString().LastIndexOf(':')
        if ($idx -ge 0) {
            $n = 0
            if ([int]::TryParse($line.ToString().Substring($idx + 1), [ref]$n)) { $total += $n }
        }
    }
    return $total
}

Push-Location $repoRoot
try {
    $results = @()
    foreach ($c in $checks) {
        $count = Get-CheckCount $c.pattern
        $results += [pscustomobject]@{
            id      = $c.id
            count   = $count
            why     = $c.why
            pattern = $c.pattern
        }
    }
}
finally { Pop-Location }

# --- baseline handling -------------------------------------------------------
if ($UpdateBaseline) {
    $map = [ordered]@{}
    foreach ($r in $results) { $map[$r.id] = $r.count }
    $baselineJson = ConvertTo-Json $map -Depth 3
    Set-Content -Path $BaselinePath -Value $baselineJson -Encoding UTF8
    Write-Host "Baseline written to $BaselinePath"
    $results | Format-Table id, count, why -AutoSize
    exit 0
}

if (-not (Test-Path $BaselinePath)) {
    Write-Error "Baseline file not found: $BaselinePath (run with -UpdateBaseline first)"
    exit 2
}
$baseline = Get-Content $BaselinePath -Raw | ConvertFrom-Json

# --- compare / report --------------------------------------------------------
$violations = @()
$improvements = @()
$report = @()
foreach ($r in $results) {
    $b = 0
    $prop = $baseline.PSObject.Properties[$r.id]
    if ($prop) { $b = [int]$prop.Value }
    $status = 'ok'
    if ($r.count -gt $b)   { $status = 'FAIL';   $violations  += $r }
    elseif ($r.count -lt $b) { $status = 'IMPROVED'; $improvements += $r }
    $report += [pscustomobject]@{
        id       = $r.id
        count    = $r.count
        baseline = $b
        status   = $status
        why      = $r.why
    }
}

if ($Json) {
    ConvertTo-Json @{ report = $report } -Depth 4
}
else {
    Write-Host ""
    Write-Host "Legacy-code lint report (baseline: $BaselinePath)" -ForegroundColor Cyan
    $report | Format-Table id, count, baseline, status, why -AutoSize
}

if ($UpdateBaseline -eq $false -and $improvements.Count -gt 0 -and -not $Json) {
    Write-Host "Counts dropped below baseline for: $($improvements.id -join ', '). " -ForegroundColor Yellow
    Write-Host "Run 'powershell -File scripts/lint.ps1 -UpdateBaseline' to ratchet down." -ForegroundColor Yellow
}

if ($violations.Count -gt 0) {
    if (-not $Json) {
        Write-Host "FAIL: legacy-code count increased in: $($violations.id -join ', ')" -ForegroundColor Red
        foreach ($v in $violations) {
            Write-Host "  [$($v.id)] $($v.count) (baseline exceeded) - $($v.why)" -ForegroundColor Red
        }
    }
    exit 1
}

if (-not $Json) { Write-Host "PASS: no lint regressions." -ForegroundColor Green }
exit 0
