#Requires -Version 7.2
[CmdletBinding()]
param(
    [string]$OutputDirectory = 'out/release',
    [string]$ExpectedTag,
    [switch]$NativeUi
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    function Invoke-BuildStep([string]$Command, [string[]]$Arguments) {
        & $Command @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit $LASTEXITCODE" }
    }

    $source = Invoke-BuildStep git @('rev-parse', 'HEAD')
    if (Invoke-BuildStep git @('status', '--porcelain', '--untracked-files=normal')) {
        throw 'Commit source changes before building a traceable package.'
    }
    $manifest = Get-Content -LiteralPath vcpkg.json -Raw | ConvertFrom-Json
    $version = $manifest.'version-semver'
    if ($version -cnotmatch '^\d+\.\d+\.\d+$' -or
        (Get-Content -LiteralPath CMakeLists.txt -Raw) -notmatch
            ('(?m)^project\(ARGUS VERSION ' + [regex]::Escape($version) + ' LANGUAGES CXX\)$')) {
        throw 'The CMake and vcpkg release versions must agree.'
    }
    if ($ExpectedTag) {
        if ($ExpectedTag -cne "v$version") { throw 'Tag does not match the package version.' }
        if ((Invoke-BuildStep git @('rev-parse', "refs/tags/$ExpectedTag^{commit}")) -cne $source) {
            throw 'Tag does not resolve to the source commit.'
        }
    }
    if (!$env:VCPKG_ROOT -or
        (Invoke-BuildStep git @('-C', $env:VCPKG_ROOT, 'rev-parse', 'HEAD')) -cne $manifest.'builtin-baseline') {
        throw 'VCPKG_ROOT must point to the registry baseline pinned in vcpkg.json.'
    }
    if (Invoke-BuildStep git @('-C', $env:VCPKG_ROOT, 'status', '--porcelain', '--untracked-files=no')) {
        throw 'The vcpkg checkout has modified tracked files.'
    }

    $build = Join-Path $root 'out/build/windows-package'
    $evidence = Join-Path $build 'release-evidence'
    $output = [IO.Path]::GetFullPath($OutputDirectory)
    [void][IO.Directory]::CreateDirectory($evidence)
    [void][IO.Directory]::CreateDirectory($output)
    $ui = if ($NativeUi) { 'ON' } else { 'OFF' }
    Invoke-BuildStep cmake @('--preset', 'windows-package', "-DARGUS_ENABLE_NATIVE_UI_TESTS=$ui")
    Invoke-BuildStep cmake @('--build', '--preset', 'windows-package', '--parallel', '2')
    $junit = Join-Path $build 'release-ctest.xml'
    Invoke-BuildStep ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure',
        '--no-tests=error', '--timeout', '90', '--output-junit', $junit)

    $results = [xml](Get-Content -LiteralPath $junit -Raw)
    $tests = @($results.SelectNodes('//testcase') | ForEach-Object {
        [ordered]@{
            name = $_.GetAttribute('name')
            result = if ($_.SelectSingleNode('failure')) { 'failed' }
                elseif ($_.SelectSingleNode('skipped')) { 'skipped' } else { 'passed' }
            seconds = [double]::Parse($_.GetAttribute('time'), [Globalization.CultureInfo]::InvariantCulture)
        }
    })
    if ($tests.Count -eq 0 -or ($tests | Where-Object { $_.result -ne 'passed' })) {
        throw 'The portable package requires a complete passing CTest run.'
    }
    [ordered]@{ count = $tests.Count; nativeUi = $NativeUi.IsPresent; tests = $tests } |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidence 'ctest.json') -Encoding utf8

    $cmakeVersionLine = (Invoke-BuildStep cmake @('--version'))[0]
    $cmakeVersion = $cmakeVersionLine -replace '^cmake version ', ''
    $compilerConfig = Join-Path $build "CMakeFiles/$cmakeVersion/CMakeCXXCompiler.cmake"
    $compiler = [regex]::Match((Get-Content -LiteralPath $compilerConfig -Raw),
        '(?m)^set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)\r?$').Groups[1].Value
    if (!$compiler) { throw 'The configured compiler version could not be recorded.' }
    $sourceDateEpoch = Invoke-BuildStep git @('show', '-s', '--format=%ct', $source)
    [ordered]@{
        version = $version
        sourceCommit = $source
        sourceDateEpoch = [long]$sourceDateEpoch
        vcpkgBaseline = $manifest.'builtin-baseline'
        cmake = $cmakeVersion
        msvc = $compiler
        windowsVersion = [Environment]::OSVersion.Version.ToString()
        preset = 'windows-package'
        triplet = 'x64-windows'
        signing = 'unsigned'
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'build.json') -Encoding utf8

    Invoke-BuildStep cmake @("-DARGUS_BUILD_DIR=$build", '-DARGUS_TRIPLET=x64-windows',
        "-DARGUS_SBOM_DIR=$evidence/sbom", '-P', 'tools/collect-vcpkg-sbom.cmake')
    $oldEpoch = $env:SOURCE_DATE_EPOCH
    try {
        $env:SOURCE_DATE_EPOCH = $sourceDateEpoch
        Invoke-BuildStep cpack @('--config', (Join-Path $build 'CPackConfig.cmake'), '-C', 'Release', '-B', $output)
    } finally {
        $env:SOURCE_DATE_EPOCH = $oldEpoch
    }
    $leaf = "ARGUS-$version-windows-x64.zip"
    $archive = Join-Path $output $leaf
    $digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$digest  $leaf" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
    Write-Host "Package: $archive"
    Write-Host "SHA-256: $digest"
} finally {
    Pop-Location
}
