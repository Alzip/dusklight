[CmdletBinding()]
param(
    [string] $BuildDirectory = 'build/korean-windows',
    [string] $OutputDirectory = 'build/korean-deliverables',
    [string] $OriginalZip = '',
    [string] $TrackerPackage = '',
    [ValidateRange(1, 32)] [int] $Parallel = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Resolve-Application([string] $Name) {
    $Application = Get-Command $Name -CommandType Application -ErrorAction Stop |
        Select-Object -First 1
    return [string] $Application.Source
}

function Invoke-Checked([string] $Executable, [string[]] $Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Native command failed with exit code ${LASTEXITCODE}: $Executable"
    }
}

function Invoke-Text([string] $Executable, [string[]] $Arguments) {
    $Lines = @(& $Executable @Arguments)
    if ($LASTEXITCODE -ne 0) {
        throw "Native command failed with exit code ${LASTEXITCODE}: $Executable"
    }
    return ($Lines -join "`n").Trim()
}

function Full-TaskPath([string] $Root, [string] $Value) {
    if ([System.IO.Path]::IsPathRooted($Value)) {
        return [System.IO.Path]::GetFullPath($Value)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Root $Value))
}

# The functions can be dot-sourced for focused PowerShell invocation tests.
if ($MyInvocation.InvocationName -eq '.') { return }

if ($env:OS -ne 'Windows_NT') {
    throw 'This build requires Windows x64 and the Visual Studio 2026 developer environment.'
}

$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Work = Full-TaskPath $Root $BuildDirectory
$Output = Full-TaskPath $Root $OutputDirectory
$BaseRevision = '40457c6adb381928e4b5fef6ed459ed291edd5e2'
$VcpkgRevision = '5f96cd15fd745122cf27e0524606d6c1efc5fd07'
$ReleaseHash = 'ac63514eeb13cd4e9bd22be02dabee4952d1331a78f0496326bba2b577b12caf'
$ReleaseUrl = 'https://github.com/TwilitRealm/dusklight/releases/download/v2.0.3/Dusklight-v2.0.3-win32-x86_64.zip'
$Git = Resolve-Application 'git'
$Python = Resolve-Application 'python'
$Cmake = Resolve-Application 'cmake'
$Compiler = Resolve-Application 'cl'
$null = Resolve-Application 'ninja'
$null = Resolve-Application 'sccache'
$Verifier = Join-Path $PSScriptRoot 'package_windows.py'

if (-not $env:VCToolsVersion -or -not $env:VCToolsVersion.StartsWith('14.51.')) {
    throw 'Use the MSVC 14.51 toolset from Visual Studio 2026, as in the official 2.0.3 build.'
}
if (-not $env:WindowsSDKVersion -or $env:WindowsSDKVersion.TrimEnd('\') -ne '10.0.26100.0') {
    throw 'Use Windows SDK 10.0.26100.0, as in the official 2.0.3 build.'
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    throw 'The Visual Studio developer environment must target x64.'
}

New-Item -ItemType Directory -Force -Path $Work | Out-Null
Push-Location $Root
try {
    Invoke-Checked $Python @($Verifier, 'audit', '--repo', $Root,
        '--output', (Join-Path $Work 'source_audit.json'))
    Invoke-Checked $Python @((Join-Path $PSScriptRoot 'generate_strings.py'), '--check')
    Invoke-Checked $Python @((Join-Path $PSScriptRoot 'verify_ui.py'), '--root', $Root)

    # A separate clean version checkout preserves the official numeric PE
    # version and AppInfo metadata. BUILD_INFO records the translated source SHA.
    # BOREALIS_APP_VERSION_OVERRIDE must not be used: it produces version 0.0.0.0.
    if (-not (Invoke-Text $Git @('tag', '--list', 'v2.0.3'))) {
        Invoke-Checked $Git @('fetch', '--no-tags', 'https://github.com/TwilitRealm/dusklight.git',
            'refs/tags/v2.0.3:refs/tags/v2.0.3')
    }
    if ((Invoke-Text $Git @('rev-parse', 'v2.0.3^{commit}')) -ne $BaseRevision) {
        throw 'The v2.0.3 tag does not point to the pinned official source revision.'
    }
    $VersionSource = Join-Path $Work 'version-source'
    if (-not (Test-Path -LiteralPath $VersionSource)) {
        Invoke-Checked $Git @('worktree', 'add', '--detach', $VersionSource, $BaseRevision)
    }
    $VersionRevision = Invoke-Text $Git @('-C', $VersionSource, 'rev-parse', 'HEAD')
    $VersionTag = Invoke-Text $Git @('-C', $VersionSource, 'describe', '--tags', '--exact-match', 'HEAD')
    $VersionDirty = Invoke-Text $Git @('-C', $VersionSource, 'status', '--porcelain', '--untracked-files=no')
    if ($VersionRevision -ne $BaseRevision -or $VersionTag -ne 'v2.0.3' -or $VersionDirty) {
        throw 'The version-only worktree must be the clean, exact official v2.0.3 tag.'
    }

    if (-not $OriginalZip) {
        $OriginalZip = Join-Path $Work 'Dusklight-v2.0.3-win32-x86_64.zip'
        if (-not (Test-Path -LiteralPath $OriginalZip)) {
            Invoke-WebRequest -Uri $ReleaseUrl -OutFile $OriginalZip
        }
    } else {
        $OriginalZip = Full-TaskPath $Root $OriginalZip
    }
    if ((Get-FileHash -LiteralPath $OriginalZip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $ReleaseHash) {
        throw 'The official Windows 2.0.3 ZIP did not match its pinned SHA-256.'
    }
    Invoke-Checked $Python @((Join-Path $PSScriptRoot 'test_package_windows.py'),
        '--original', $OriginalZip, '-v')

    # Isolated vcpkg snapshot: these eight ports match the original release job.
    # Do not use or update the runner's global vcpkg installation.
    $DependencyRoot = Join-Path $Work 'vcpkg'
    if (-not (Test-Path -LiteralPath $DependencyRoot)) {
        New-Item -ItemType Directory -Path $DependencyRoot | Out-Null
        Invoke-Checked $Git @('-C', $DependencyRoot, 'init')
        Invoke-Checked $Git @('-C', $DependencyRoot, 'remote', 'add', 'origin', 'https://github.com/microsoft/vcpkg.git')
        Invoke-Checked $Git @('-C', $DependencyRoot, 'fetch', '--depth', '1', 'origin', $VcpkgRevision)
        Invoke-Checked $Git @('-C', $DependencyRoot, 'checkout', '--detach', 'FETCH_HEAD')
    }
    if ((Invoke-Text $Git @('-C', $DependencyRoot, 'rev-parse', 'HEAD')) -ne $VcpkgRevision) {
        throw 'The isolated vcpkg checkout does not match the pinned revision.'
    }
    if (Invoke-Text $Git @('-C', $DependencyRoot, 'status', '--porcelain', '--untracked-files=no')) {
        throw 'The pinned vcpkg sources have local modifications.'
    }
    $env:VCPKG_ROOT = $DependencyRoot
    $env:VCPKG_DISABLE_METRICS = '1'
    $Vcpkg = Join-Path $DependencyRoot 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $Vcpkg)) {
        Invoke-Checked (Join-Path $DependencyRoot 'bootstrap-vcpkg.bat') @('-disableMetrics')
    }
    Invoke-Checked $Vcpkg @('install', 'freetype:x64-windows', 'zstd:x64-windows', '--disable-metrics')
    $PortList = Join-Path $Work 'vcpkg-ports.txt'
    (Invoke-Text $Vcpkg @('list')) | Set-Content -LiteralPath $PortList -Encoding utf8
    Invoke-Checked $Python @($Verifier, 'ports', '--installed', $PortList)

    $CmakeBuild = Join-Path $Work 'cmake'
    $ConfigureArguments = @(
        '--preset', 'x-windows-ci-msvc', '-B', $CmakeBuild,
        "-DBOREALIS_APP_SOURCE_DIR=$VersionSource",
        '-DBOREALIS_ENABLE_SENTRY=ON', '-DBOREALIS_SENTRY_DSN=',
        '-DAURORA_DAWN_PROVIDER=package', '-DAURORA_SDL3_PROVIDER=package',
        '-DAURORA_NOD_PROVIDER=package', '-DVCPKG_MANIFEST_MODE=OFF'
    )
    Invoke-Checked $Cmake $ConfigureArguments

    $FmtInclude = Join-Path $CmakeBuild '_deps/fmt-src/include'
    if (-not (Test-Path -LiteralPath (Join-Path $FmtInclude 'fmt/format.h'))) {
        throw 'The expected pinned fmt include directory was not generated by CMake.'
    }
    Invoke-Checked $Python @((Join-Path $PSScriptRoot 'test_generate_strings.py'),
        '--fmt-include', $FmtInclude, '-v')
    # Building only the host avoids rebuilding or replacing the bundled mods.
    # The host target still generates its exports, import library and symdb.
    Invoke-Checked $Cmake @('--build', $CmakeBuild, '--target', 'dusklight', '--parallel', [string] $Parallel)
    $Executable = Join-Path $CmakeBuild 'dusklight.exe'
    if (-not (Test-Path -LiteralPath $Executable)) { throw 'The Windows executable was not produced.' }
    $Toolchain = [ordered] @{
        compiler = 'MSVC'
        compiler_file_version = (Get-Item -LiteralPath $Compiler).VersionInfo.FileVersion
        vc_tools_version = $env:VCToolsVersion
        windows_sdk_version = $env:WindowsSDKVersion.TrimEnd('\')
        cmake = Invoke-Text $Cmake @('--version')
        ninja = Invoke-Text (Resolve-Application 'ninja') @('--version')
        sccache = Invoke-Text (Resolve-Application 'sccache') @('--version')
        vcpkg_revision = $VcpkgRevision
        host_build_type = 'RelWithDebInfo'
        preset = 'x-windows-ci-msvc'
        runner_image = $env:ImageOS
        runner_image_version = $env:ImageVersion
        localization_checks = @('generate_strings.py --check', 'verify_ui.py (source equivalence and font coverage)', 'test_generate_strings.py (including MSVC fmt compile probes)')
        packaging_checks = 'test_package_windows.py; original exports/imports/version/resource hash gates'
    }
    $ToolchainPath = Join-Path $Work 'toolchain.json'
    $Toolchain | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ToolchainPath -Encoding utf8
    $PackageArguments = @($Verifier, 'package', '--repo', $Root,
        '--original', $OriginalZip, '--executable', $Executable,
        '--toolchain', $ToolchainPath, '--output', $Output)
    if ($TrackerPackage) {
        $PackageArguments += @('--tracker', (Full-TaskPath $Root $TrackerPackage))
    }
    Invoke-Checked $Python $PackageArguments
} finally {
    Pop-Location
}
