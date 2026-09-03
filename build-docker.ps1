# PKHeX-GC: build pkhex-gc.dol in the devkitPPC container on Windows.
#
#   .\build-docker.ps1            build the DOL
#   .\build-docker.ps1 --tests    host tests only
#
# Requires Docker Desktop. On WSL or Git Bash, ./build.sh is equivalent and
# can also use a locally installed devkitPPC.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$image = if ($env:DEVKITPPC_IMAGE) { $env:DEVKITPPC_IMAGE } else { 'devkitpro/devkitppc:20260503' }

docker run --rm -v "${root}:/project" -w /project $image bash -lc "./build.sh --native $($args -join ' ')"
if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }

Write-Host ''
Write-Host 'Built pkhex-gc.dol - copy it to your SD card and launch it from Swiss.'
