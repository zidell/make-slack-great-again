#Requires -Version 5.1
# Publishes this fork's Windows release to GitHub Releases
# (zidell/make-slack-great-again), where the fork's builds look for updates
# (src/app/update/fork_release.h). The Windows twin of fork-release.sh: run
# on the same commit, both add their half to the same release.
#
# The release is v<MSGA_VERSION>.<N>: HEAD already tagged -> that release,
# otherwise the next N on this MSGA_VERSION (1 after an upstream version
# bump), tagged and pushed here.
#
# Uploads msga-windows-x86_64.exe and msga-windows-x86_64.manifest
# ({"version": MSGA_VERSION * 100 + N, "sha256": ...}), the names the updater
# asks for. Needs MSYS2 (msys2.ps1), git and gh (gh auth login).
#
# Usage: scripts\fork-release.ps1    (master, clean, pushed to origin)

$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
. (Join-Path $ScriptDir 'msys2.ps1')

if ($args.Count -gt 0) { die "unknown argument: $($args[0])" }

$Repo     = 'zidell/make-slack-great-again'
$Asset    = 'msga-windows-x86_64'
$BuildDir = Join-Path $ProjectRoot 'build-fork-release'
$DistDir  = Join-Path $BuildDir 'dist'
$Nproc    = $env:NUMBER_OF_PROCESSORS

function git-out { $o = & git -C $ProjectRoot @args; if ($LASTEXITCODE -ne 0) { die "git $args failed" }; $o }

if ((git-out branch --show-current) -ne 'master') { die 'not on master' }
if (git-out status --porcelain) { die 'uncommitted changes' }
git-out fetch -q origin master --tags | Out-Null
if ((git-out rev-parse HEAD) -ne (git-out rev-parse origin/master)) { die 'HEAD is not origin/master (push first)' }
foreach ($exe in 'gh') {
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) { die "$exe not found" }
}

$m = Select-String -Path (Join-Path $ProjectRoot 'version.cmake') -Pattern '^set\(MSGA_VERSION (\d+)\)'
if (-not $m) { die 'no MSGA_VERSION in version.cmake' }
$Base = [int]$m.Matches[0].Groups[1].Value

# The release: HEAD's tag, or the next one on this base.
$numbers = @(git-out tag --points-at HEAD --list "v$Base.*" | ForEach-Object { [int]($_ -replace "^v$Base\.", '') } | Sort-Object)
$Prev = ''
if ($numbers.Count -gt 0) {
    $N = $numbers[-1]
    $Tag = "v$Base.$N"
} else {
    $all = @(git-out tag --list "v$Base.*" | ForEach-Object { [int]($_ -replace "^v$Base\.", '') } | Sort-Object)
    $N = if ($all.Count -gt 0) { $all[-1] + 1 } else { 1 }
    if ($N -ge 100) { die "v$Base.${N}: the updater's number only has room for 99" }
    $Tag = "v$Base.$N"
    $Prev = @(git-out tag --list 'v*.*' --sort=-v:refname)[0]
    git-out tag -a $Tag -m "msga $Base.$N" | Out-Null
    git-out push -q origin $Tag | Out-Null
    Write-Host "tagged $Tag"
}
$Version = $Base * 100 + $N

# The release flags of release.ps1, plus the fork's release number and update
# checks. Reconfigured every time (cached options).
Use-Mingw64
if ((Test-Path $BuildDir) -and -not (Test-Path (Join-Path $BuildDir 'build.ninja'))) {
    Remove-Item $BuildDir -Recurse -Force
}
cmake -S $ProjectRoot -B $BuildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=MinSizeRel `
    -DMSGA_STATIC=ON `
    -DMSGA_DEMO=OFF `
    -DMSGA_BUILD_TESTS=OFF `
    "-DMSGA_FORK_RELEASE=$N" `
    -DMSGA_UPDATES=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Set-Content -Path (Join-Path $BuildDir '.gitignore') -Value '*' # all of it generated
cmake --build $BuildDir --target msga --parallel $Nproc
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# mingw links with ld.bfd and no -s (see CMakeLists.txt), so strip here.
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
$Exe = Join-Path $DistDir "$Asset.exe"
Copy-Item (Join-Path $BuildDir 'msga.exe') $Exe -Force
& (Join-Path $MingwBin 'strip.exe') $Exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Sha = (Get-FileHash -Algorithm SHA256 $Exe).Hash.ToLowerInvariant()
$Manifest = Join-Path $DistDir "$Asset.manifest"
[IO.File]::WriteAllText($Manifest, "{`"version`":$Version,`"sha256`":`"$Sha`"}`n")

& gh release view $Tag -R $Repo *> $null
if ($LASTEXITCODE -eq 0) {
    gh release upload $Tag $Exe $Manifest -R $Repo --clobber
} else {
    $range = if ($Prev) { @("$Prev..$Tag") } else { @() }
    $Notes = (git-out log --no-merges --format='- %s' @range -n 50) -join "`n"
    gh release create $Tag $Exe $Manifest -R $Repo --latest --title "msga $Base.$N" --notes $Notes
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "published $Tag ($Version): $Asset.exe, $Asset.manifest"
