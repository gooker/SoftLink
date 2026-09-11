param(
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$Tag = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repositoryRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repositoryRoot
try {
    $cmakeText = Get-Content -LiteralPath CMakeLists.txt -Raw
    $versionMatch = [regex]::Match($cmakeText, 'project\(SoftLink VERSION (\d+\.\d+\.\d+)\b')
    if (-not $versionMatch.Success) { throw 'CMake project version was not found.' }
    $version = $versionMatch.Groups[1].Value
    if ($Tag -and $Tag -cne "v$version") { throw "Tag must match the project version: v$version" }
    [xml]$manifest = Get-Content -LiteralPath src/SoftLink.manifest -Raw
    if ($manifest.assembly.assemblyIdentity.version -ne "$version.0") {
        throw 'The application manifest version must match the CMake project version.'
    }

    cmake -S . -B build/release -G $Generator -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    cmake --build build/release --config MinSizeRel
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }

    $artifactName = "SoftLink-v$version-windows-x64"
    $outputDirectory = Join-Path $repositoryRoot "dist/v$version"
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $executable = Join-Path $outputDirectory "$artifactName.exe"
    $archive = Join-Path $outputDirectory "$artifactName.zip"
    Copy-Item -LiteralPath dist/SoftLink.exe -Destination $executable -Force

    # Use a fresh staging directory so old builds and local history cannot enter the archive.
    $stagingDirectory = Join-Path $repositoryRoot ('build/package-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path (Join-Path $stagingDirectory 'src/third_party') -Force | Out-Null
    Copy-Item -LiteralPath dist/SoftLink.exe, README.md, README_CN.md -Destination $stagingDirectory
    Copy-Item -LiteralPath src/README.md -Destination (Join-Path $stagingDirectory 'src')
    Copy-Item -LiteralPath src/third_party/picojson.LICENSE.txt -Destination (Join-Path $stagingDirectory 'src/third_party')
    Compress-Archive -LiteralPath (Join-Path $stagingDirectory 'SoftLink.exe'),
        (Join-Path $stagingDirectory 'README.md'), (Join-Path $stagingDirectory 'README_CN.md'),
        (Join-Path $stagingDirectory 'src') -DestinationPath $archive -Force

    $checksums = foreach ($artifact in @($executable, $archive)) {
        $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
        '{0}  {1}' -f $hash, [System.IO.Path]::GetFileName($artifact)
    }
    $checksums | Set-Content -LiteralPath (Join-Path $outputDirectory 'SHA256SUMS.txt') -Encoding ascii
    Get-Item -LiteralPath $executable, $archive | Select-Object FullName, Length
} finally {
    Pop-Location
}
