# SPDX-License-Identifier: AGPL-3.0-or-later
<#
.SYNOPSIS
Builds, tests and packages Brulion for Windows x86_64 (the test version, docs/WINDOWS.md).

.DESCRIPTION
Steps (all by default, in this order; -Steps picks some):
  build    CMake (Ninja, Release) + MSVC 2022 against Qt 6.11.2 msvc2022_64
  test     ctest offscreen (the tests that run on Windows: the CMake files leave out the bash ones)
  package  windeployqt (Qt libraries, plugins, the QML modules src/app/qml imports) and the MSVC
           runtime next to brulion.exe and brulion-cli.exe, then
             dist-windows\brulion-<version>-windows-x86_64.zip          (portable)
             dist-windows\Brulion-<version>-windows-x86_64-setup.exe    (Inno Setup,
                                                                          packaging\windows\brulion.iss)
  check    the packages on this machine, without Qt on PATH: the ZIP unpacked into a fresh folder
           and the installer installed silently for the current user (with the task "autostart":
           the Run value must start the installed brulion.exe --hidden), each started headless
           (brulion-cli --version, the shell self-test of the real app in a separate profile,
           which registers its AppUserModelID for the toasts with the icon next to the program);
           the installer offers Brulion in "Open with" for .md, .markdown and .txt (the ZIP
           registers nothing) without changing their default program; then installed again as a
           silent update with the Run value removed (it must stay removed), then uninstalled
           silently — the profile's settings and notes must still be there, the Run value and the
           "Open with" registration must be gone.

CI: .github/workflows/windows.yml (windows-2022, Qt from aqt through jurplel/install-qt-action,
MSVC from ilammy/msvc-dev-cmd). Locally, in PowerShell from the repository root, with Visual Studio
2022 (C++ workload, it brings CMake and Ninja), Qt 6.11.2 msvc2022_64 (e.g.
`aqt install-qt windows desktop 6.11.2 win64_msvc2022_64 -O C:\Qt`) and Inno Setup 6:
  powershell -ExecutionPolicy Bypass -File scripts\windows-build.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64
Without the Visual Studio environment (cl.exe not on PATH) the script loads it with vswhere.

.PARAMETER Steps
Any of build, test, package, check (default: all four).
.PARAMETER QtDir
Qt for MSVC (has lib\cmake\Qt6); default $env:QT_ROOT_DIR (set by install-qt-action).
.PARAMETER BuildDir
Build directory (default build-windows).
.PARAMETER DistDir
Output directory of the ZIP and the installer (default dist-windows).
.PARAMETER Jobs
Parallel compile and test jobs (default: all processors).
#>
[CmdletBinding()]
param(
    [ValidateSet('build', 'test', 'package', 'check')]
    [string[]] $Steps = @('build', 'test', 'package', 'check'),
    [string] $QtDir = $env:QT_ROOT_DIR,
    [string] $BuildDir = 'build-windows',
    [string] $DistDir = 'dist-windows',
    [int] $Jobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Info([string] $Message) { Write-Host "`n==> $Message" -ForegroundColor Cyan }
function Fail([string] $Message) {
    if ($env:GITHUB_ACTIONS) { Write-Host "::error::$Message" }
    throw $Message
}
# Runs a native program and fails on a non-zero exit code (PowerShell 5 does not).
function Exec([string] $Program, [string[]] $Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { Fail "$Program exited with code $LASTEXITCODE" }
}
function Full([string] $Path) { [IO.Path]::GetFullPath((Join-Path $Root $Path)) }

$Version = (Select-String -Path CMakeLists.txt -Pattern '^\s*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' |
    Select-Object -First 1).Matches[0].Groups[1].Value
if (-not $Version) { Fail 'Cannot read the project version from CMakeLists.txt' }
$BuildDir = Full $BuildDir
$DistDir = Full $DistDir
$Stage = Join-Path $BuildDir 'package\Brulion'
$ZipName = "brulion-$Version-windows-x86_64.zip"
$SetupName = "Brulion-$Version-windows-x86_64-setup.exe"
$TempRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }

Info "Brulion $Version for Windows x86_64: $($Steps -join ', ')"

# --- Tools ---------------------------------------------------------------------------------------

function Enter-Msvc {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { Fail 'No cl.exe on PATH and no vswhere.exe: install Visual Studio 2022 with C++' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { Fail 'Visual Studio with the x64 C++ tools not found' }
    Info "Loading the MSVC environment of $vs"
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    & cmd.exe /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { Fail "vcvars64.bat did not put cl.exe on PATH" }
}

function Assert-Qt {
    if (-not $QtDir -or -not (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6\Qt6Config.cmake'))) {
        Fail "No Qt for MSVC at '$QtDir': pass -QtDir <Qt>\6.11.2\msvc2022_64 (or set QT_ROOT_DIR)"
    }
}

function Find-Iscc {
    $command = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($dir in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, (Join-Path $env:LOCALAPPDATA 'Programs'))) {
        if (-not $dir) { continue }
        $candidate = Join-Path $dir 'Inno Setup 6\ISCC.exe'
        if (Test-Path $candidate) { return $candidate }
    }
    Fail 'Inno Setup 6 (ISCC.exe) not found: install it (winget install JRSoftware.InnoSetup, choco install innosetup)'
}

# --- build ---------------------------------------------------------------------------------------

if ($Steps -contains 'build') {
    Enter-Msvc
    Assert-Qt
    foreach ($tool in 'cmake', 'ninja') {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool not found (Visual Studio's CMake tools bring both)" }
    }
    Info "Configuring ($BuildDir)"
    $configure = @('-S', $Root, '-B', $BuildDir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$QtDir", '-DBUILD_TESTING=ON')
    # ccache (4.6 or newer knows cl.exe; Release has no /Zi, whose PDBs it cannot cache) when it is
    # on PATH: CI restores its cache, a rebuild of the same sources takes minutes instead of 15.
    $ccache = Get-Command ccache -ErrorAction SilentlyContinue
    if ($ccache) {
        Write-Host "Compiler cache: $($ccache.Source)"
        $configure += @("-DCMAKE_C_COMPILER_LAUNCHER=$($ccache.Source)", "-DCMAKE_CXX_COMPILER_LAUNCHER=$($ccache.Source)")
    }
    Exec cmake $configure
    Info "Building (-j$Jobs)"
    # -k 0: every compile error in one run (the CI round trip is long), not only the first.
    Exec cmake @('--build', $BuildDir, '--parallel', "$Jobs", '--', '-k', '0')
}

# --- test ----------------------------------------------------------------------------------------

if ($Steps -contains 'test') {
    Assert-Qt
    Info 'Unit tests (ctest, offscreen)'
    # The tests run from the build directory: Qt's DLLs come from Qt's bin folder.
    $env:PATH = "$(Join-Path $QtDir 'bin');$env:PATH"
    $env:QT_QPA_PLATFORM = 'offscreen'
    # Without a console window Qt (and QtTest's report) writes to the debugger, not to the output
    # ctest shows; the offscreen platform looks for fonts in Qt's own lib\fonts, which is empty.
    $env:QT_FORCE_STDERR_LOGGING = '1'
    $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
    Exec ctest @('--test-dir', $BuildDir, '--output-on-failure', '--parallel', "$Jobs", '--timeout', '600')
}

# --- package -------------------------------------------------------------------------------------

if ($Steps -contains 'package') {
    Enter-Msvc
    Assert-Qt
    Info "Deploying into $Stage"
    if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
    New-Item -ItemType Directory -Force -Path $Stage | Out-Null
    # qt_standard_project_setup() puts every executable into the build folder's top on Windows
    # (next to the DLLs it builds); a build with its own CMAKE_RUNTIME_OUTPUT_DIRECTORY elsewhere.
    foreach ($exe in @(@('brulion.exe', 'src\app'), @('brulion-cli.exe', 'src\cli'))) {
        $built = @((Join-Path $BuildDir $exe[0]), (Join-Path (Join-Path $BuildDir $exe[1]) $exe[0])) |
            Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $built) { Fail "$($exe[0]) not found in $BuildDir (run the build step)" }
        Copy-Item $built $Stage
    }
    Copy-Item (Join-Path $Root 'LICENSE') (Join-Path $Stage 'LICENSE.txt')
    # The icon of the toasts (the AppUserModelID's IconUri, src/platform/appidentity.h).
    Copy-Item (Join-Path $Root 'packaging\windows\brulion.ico') $Stage
    $skill = Join-Path $Stage 'agent-skills\brulion'
    New-Item -ItemType Directory -Force -Path $skill | Out-Null
    Copy-Item (Join-Path $Root 'packaging\agent\brulion\SKILL.md') $skill

    # Qt: libraries, plugins (platforms, image formats incl. SVG, TLS — Schannel needs no OpenSSL —,
    # styles) and the QML modules the app's QML imports. No Qt translations (the app has its own,
    # compiled in) and no software OpenGL (Qt Quick draws with Direct3D 11, WARP without a GPU).
    $windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
    Exec $windeployqt @('--release', '--qmldir', (Join-Path $Root 'src\app\qml'), '--no-translations',
        '--no-opengl-sw', '--no-compiler-runtime', '--verbose', '1', (Join-Path $Stage 'brulion.exe'))
    # The offscreen platform too (windeployqt takes only the windows one; it needs nothing more):
    # headless runs of the packaged app — the check below, screenshots — work.
    Copy-Item (Join-Path $QtDir 'plugins\platforms\qoffscreen.dll') (Join-Path $Stage 'platforms')
    Exec $windeployqt @('--release', '--no-translations', '--no-opengl-sw', '--no-compiler-runtime',
        '--verbose', '1', (Join-Path $Stage 'brulion-cli.exe'))

    # The MSVC runtime next to the executables (app-local, as Microsoft allows): no Visual C++
    # Redistributable to install first. Windows 10 and 11 have the Universal CRT.
    if (-not $env:VCToolsRedistDir) { Fail 'VCToolsRedistDir is not set (no MSVC environment)' }
    $crt = Get-ChildItem -Directory (Join-Path $env:VCToolsRedistDir 'x64') -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
    if (-not $crt) { Fail "No Microsoft.VC*.CRT in $env:VCToolsRedistDir\x64" }
    Copy-Item (Join-Path $crt.FullName '*.dll') $Stage
    Write-Host "MSVC runtime: $($crt.Name) ($((Get-ChildItem $crt.FullName -Filter *.dll).Name -join ', '))"

    New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
    $zip = Join-Path $DistDir $ZipName
    $setup = Join-Path $DistDir $SetupName
    Remove-Item -Force -ErrorAction SilentlyContinue $zip, $setup

    Info "Packing $ZipName"
    # The archive holds the folder "Brulion": unpacked anywhere, brulion.exe runs from there.
    Compress-Archive -Path $Stage -DestinationPath $zip -CompressionLevel Optimal

    Info "Building $SetupName (Inno Setup)"
    $iscc = Find-Iscc
    Exec $iscc @('/Qp', "/DAppVersion=$Version", "/DSourceDir=$Stage", "/DOutputDir=$DistDir",
        (Join-Path $Root 'packaging\windows\brulion.iss'))
    if (-not (Test-Path $setup)) { Fail "Inno Setup produced no $setup" }

    Get-ChildItem $DistDir | ForEach-Object {
        Write-Host ("{0}  {1,8:N1} MB  {2}" -f (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower(), ($_.Length / 1MB), $_.Name)
    }
    if ($env:GITHUB_OUTPUT) {
        Add-Content -Path $env:GITHUB_OUTPUT -Value "version=$Version"
        Add-Content -Path $env:GITHUB_OUTPUT -Value "zip=$zip"
        Add-Content -Path $env:GITHUB_OUTPUT -Value "setup=$setup"
    }
}

# --- check ---------------------------------------------------------------------------------------

# A headless run of a packaged brulion.exe in a fresh separate profile (BRULION_PROFILE), with
# nothing of Qt on PATH or in Qt's search variables: whatever is missing from the package fails it.
$CheckProfile = 'package-check'
$ProfileDirs = @((Join-Path $env:APPDATA "Brulion-$CheckProfile"), (Join-Path $env:LOCALAPPDATA "Brulion-$CheckProfile"))
# The profile's AppUserModelID (src/platform/appidentity.h) and the start at sign-in (autostart.h).
$AumidKey = "HKCU:\Software\Classes\AppUserModelId\JanuszBit.Brulion.$CheckProfile"
$RunKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'

function Get-RunValue {
    $item = Get-ItemProperty -Path $RunKey -Name 'Brulion' -ErrorAction SilentlyContinue
    if ($item) { return $item.Brulion }
    return $null
}

# The run registered the profile's AppUserModelID with the icon next to the program.
function Assert-AppIdentity([string] $Dir, [string] $Label) {
    if (-not (Test-Path $AumidKey)) { Fail "$Label`: no AppUserModelID registration $AumidKey" }
    $registration = Get-ItemProperty -Path $AumidKey
    $icon = Join-Path $Dir 'brulion.ico'
    if (-not (Test-Path $icon)) { Fail "$Label`: $icon missing" }
    if ($registration.IconUri -ne $icon) { Fail "$Label`: IconUri is '$($registration.IconUri)', not $icon" }
    Write-Host "$Label`: AppUserModelID registered ($($registration.DisplayName), $($registration.IconUri))"
    Remove-Item -Recurse -Force $AumidKey
}

# "Otwórz za pomocą → Brulion" (packaging/windows/brulion.iss, docs/WINDOWS.md): the installer's
# ProgID, each type's OpenWithProgids and Applications\brulion.exe; never a type's default.
$Classes = 'HKCU:\Software\Classes'
$NoteProgId = 'JanuszBit.Brulion.Note'
$OpenWithTypes = @('.md', '.markdown', '.txt')
$FileExts = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts'

# The default value of a key ($null when the key or the value is missing).
function Get-DefaultValue([string] $Key) {
    $item = Get-Item -LiteralPath $Key -ErrorAction SilentlyContinue
    if (-not $item) { return $null }
    return $item.GetValue('')
}

# The value names of a key (none when it is missing).
function Get-ValueNames([string] $Key) {
    $item = Get-Item -LiteralPath $Key -ErrorAction SilentlyContinue
    if (-not $item) { return @() }
    return @($item.GetValueNames())
}

# What decides the program a type opens with: its default value and the user's choice.
function Get-TypeDefaults {
    $defaults = @{}
    foreach ($type in $OpenWithTypes) {
        $defaults[$type] = "default='$(Get-DefaultValue "$Classes\$type")' choice='$(Get-ItemProperty -LiteralPath "$FileExts\$type\UserChoice" -Name 'ProgId' -ErrorAction SilentlyContinue | ForEach-Object { $_.ProgId })'"
    }
    return $defaults
}

function Assert-OpenWith([string] $Dir, [string] $Label, [hashtable] $DefaultsBefore) {
    $expected = "`"$(Join-Path $Dir 'brulion.exe')`" --open `"%1`""
    foreach ($key in "$Classes\$NoteProgId\shell\open\command", "$Classes\Applications\brulion.exe\shell\open\command") {
        $command = Get-DefaultValue $key
        if ($command -ne $expected) { Fail "$Label`: $key is '$command', not '$expected'" }
    }
    foreach ($type in $OpenWithTypes) {
        if ((Get-ValueNames "$Classes\$type\OpenWithProgids") -notcontains $NoteProgId) { Fail "$Label`: $type does not offer Brulion (OpenWithProgids)" }
        if ((Get-ValueNames "$Classes\Applications\brulion.exe\SupportedTypes") -notcontains $type) { Fail "$Label`: Applications\brulion.exe does not list $type" }
    }
    $after = Get-TypeDefaults
    foreach ($type in $OpenWithTypes) {
        if ($after[$type] -ne $DefaultsBefore[$type]) { Fail "$Label`: the default program of $type changed: $($DefaultsBefore[$type]) -> $($after[$type])" }
    }
    Write-Host "$Label`: Open with -> $expected for $($OpenWithTypes -join ', '); defaults unchanged"
}

# Nothing of the registration is left (a type's key itself may stay: it may hold other programs).
function Assert-NoOpenWith([string] $Label) {
    foreach ($key in "$Classes\$NoteProgId", "$Classes\Applications\brulion.exe") {
        if (Test-Path -LiteralPath $key) { Fail "$Label`: $key exists" }
    }
    foreach ($type in $OpenWithTypes) {
        if ((Get-ValueNames "$Classes\$type\OpenWithProgids") -contains $NoteProgId) { Fail "$Label`: $type still offers Brulion" }
    }
}

function Invoke-Packaged([string] $Dir, [string] $Label) {
    $exe = Join-Path $Dir 'brulion.exe'
    $cli = Join-Path $Dir 'brulion-cli.exe'
    foreach ($file in $exe, $cli) { if (-not (Test-Path $file)) { Fail "$Label`: $file missing" } }
    $saved = @{}
    foreach ($name in 'PATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_ROOT_DIR', 'Qt6_DIR',
        'QT_QPA_PLATFORM', 'QT_QUICK_BACKEND', 'QT_FORCE_STDERR_LOGGING', 'QT_QPA_FONTDIR', 'BRULION_PROFILE') {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"
        foreach ($name in 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_ROOT_DIR', 'Qt6_DIR') {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QT_QUICK_BACKEND = 'software'
        $env:QT_FORCE_STDERR_LOGGING = '1'
        $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts' # the offscreen platform's fonts
        $env:BRULION_PROFILE = $CheckProfile

        Info "$Label`: brulion-cli --version"
        # (Windows PowerShell 5 turns a native program's stderr into errors: not here.)
        $ErrorActionPreference = 'Continue'
        $out = & $cli --version 2>&1 | Out-String
        $ErrorActionPreference = 'Stop'
        Write-Host $out.Trim()
        if ($LASTEXITCODE -ne 0) { Fail "$Label`: brulion-cli --version exited with $LASTEXITCODE" }
        if ($out -notmatch [regex]::Escape($Version)) { Fail "$Label`: brulion-cli --version does not say $Version" }

        Info "$Label`: brulion.exe --selftest-shell (offscreen)"
        # (Not $dir: PowerShell names are case-insensitive, that is the parameter $Dir.)
        foreach ($old in $ProfileDirs) { if (Test-Path $old) { Remove-Item -Recurse -Force $old } }
        # brulion.exe is a GUI program (no console): started directly, with its output captured
        # (UTF-8) and its exit code. The environment is this process's, as changed above.
        $start = [Diagnostics.ProcessStartInfo]::new($exe)
        $start.Arguments = '--demo --route journals --selftest-shell --no-single-instance --size 412x915 --touch ' +
            '--now "2026-10-14 10:00" --locale pl'
        $start.WorkingDirectory = $Dir
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $start.StandardOutputEncoding = [Text.Encoding]::UTF8
        $start.StandardErrorEncoding = [Text.Encoding]::UTF8
        $process = [Diagnostics.Process]::Start($start)
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $finished = $process.WaitForExit(300000)
        if (-not $finished) { $process.Kill() }
        $process.WaitForExit() # the output's end too
        $output = $stdout.Result + $stderr.Result
        if (-not $finished) {
            if ($output) { ($output -split "`r?`n" | Select-Object -Last 60) -join "`n" | Write-Host }
            Fail "$Label`: brulion.exe --selftest-shell did not finish in 5 minutes (output above)"
        }
        if ($output) {
            $lines = $output -split "`r?`n"
            # The failed checks first: the last 60 lines often start after them.
            $failed = $lines | Where-Object { $_ -match '^selftest: FAIL ' }
            if ($failed) { "Failed checks:`n" + ($failed -join "`n") | Write-Host }
            ($lines | Select-Object -Last 60) -join "`n" | Write-Host
        }
        if ($process.ExitCode -ne 0) { Fail "$Label`: brulion.exe --selftest-shell exited with $($process.ExitCode)" }
        if ($output -match '(?i)is not installed|module .* not found|cannot load library|could not load the qt platform plugin|failed to load|TypeError|ReferenceError') {
            Fail "$Label`: Qt or QML errors in the run (above)"
        }
    } finally {
        foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
    }
}

if ($Steps -contains 'check') {
    $zip = Join-Path $DistDir $ZipName
    $setup = Join-Path $DistDir $SetupName
    foreach ($file in $zip, $setup) { if (-not (Test-Path $file)) { Fail "$file missing (run the package step)" } }

    $work = Join-Path $TempRoot "brulion-package-check-$PID"
    New-Item -ItemType Directory -Force -Path $work | Out-Null

    # Portable: unpacked into a fresh folder.
    Info "Unpacking $ZipName into $work\zip"
    Expand-Archive -Path $zip -DestinationPath (Join-Path $work 'zip')
    if (Get-RunValue) { Fail "A start at sign-in entry exists already: $(Get-RunValue)" }
    Invoke-Packaged (Join-Path $work 'zip\Brulion') 'ZIP'
    Assert-AppIdentity (Join-Path $work 'zip\Brulion') 'ZIP'
    # A separate profile never writes the Run key.
    if (Get-RunValue) { Fail "The ZIP's run wrote a start at sign-in entry: $(Get-RunValue)" }
    Assert-NoOpenWith 'ZIP'
    $typeDefaults = Get-TypeDefaults

    # Installer: per user, silent; then the installed copy; then the uninstaller.
    $installDir = Join-Path $env:LOCALAPPDATA 'Programs\Brulion'
    if (Test-Path (Join-Path $installDir 'brulion.exe')) { Fail "Brulion is already installed in ${installDir}: uninstall it first" }
    Info "Installing $SetupName (silent, current user)"
    $install = Start-Process -FilePath $setup -PassThru -Wait -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES',
        '/CURRENTUSER', '/NORESTART', '/TASKS=autostart', "/LOG=`"$work\install.log`"")
    if ($install.ExitCode -ne 0) { Get-Content "$work\install.log" | Write-Host; Fail "The installer exited with $($install.ExitCode)" }
    if (-not (Test-Path (Join-Path $installDir 'brulion.exe'))) { Get-Content "$work\install.log" | Write-Host; Fail "Not installed into $installDir" }
    $shortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Brulion.lnk'
    if (-not (Test-Path $shortcut)) { Fail "No Start menu shortcut $shortcut" }
    $expectedRun = "`"$(Join-Path $installDir 'brulion.exe')`" --hidden"
    if ((Get-RunValue) -ne $expectedRun) { Fail "The task autostart wrote '$(Get-RunValue)', not '$expectedRun'" }
    Write-Host "Start at sign-in: $(Get-RunValue)"
    Invoke-Packaged $installDir 'Installed'
    Assert-AppIdentity $installDir 'Installed'
    Assert-OpenWith $installDir 'Installed' $typeDefaults

    # An update: autostart turned off (as the app's switch does) stays off — the task is offered on
    # the first install only (UsePreviousTasks would select it again).
    Remove-ItemProperty -Path $RunKey -Name 'Brulion'
    Info "Installing $SetupName again (silent update)"
    $update = Start-Process -FilePath $setup -PassThru -Wait -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES',
        '/CURRENTUSER', '/NORESTART', "/LOG=`"$work\update.log`"")
    if ($update.ExitCode -ne 0) { Get-Content "$work\update.log" | Write-Host; Fail "The update exited with $($update.ExitCode)" }
    if (Get-RunValue) { Get-Content "$work\update.log" | Write-Host; Fail "The update turned the start at sign-in on again: $(Get-RunValue)" }
    Write-Host 'Update: the start at sign-in stays off'
    Assert-OpenWith $installDir 'Update' $typeDefaults
    # On again for the uninstaller's check below.
    Set-ItemProperty -Path $RunKey -Name 'Brulion' -Value $expectedRun

    Info 'Uninstalling (silent)'
    $settings = Join-Path $env:APPDATA "Brulion-$CheckProfile\Brulion.ini"
    if (-not (Test-Path $settings)) { Fail "The run left no settings at $settings" }
    $uninstaller = Join-Path $installDir 'unins000.exe'
    if (-not (Test-Path $uninstaller)) { Fail "No uninstaller $uninstaller" }
    Start-Process -FilePath $uninstaller -Wait -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') | Out-Null
    # The uninstaller hands over to a copy of itself in the temp folder: wait for the files to go.
    $deadline = (Get-Date).AddMinutes(2)
    while ((Test-Path (Join-Path $installDir 'brulion.exe')) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 1 }
    if (Test-Path (Join-Path $installDir 'brulion.exe')) { Fail "The uninstaller left $installDir\brulion.exe" }
    if (Test-Path $shortcut) { Fail "The uninstaller left the Start menu shortcut" }
    if (Get-RunValue) { Fail "The uninstaller left the start at sign-in entry: $(Get-RunValue)" }
    Assert-NoOpenWith 'Uninstalled'
    Write-Host 'Uninstalled: the Open with registration is gone'
    # The user's settings and notes are never the uninstaller's.
    if (-not (Test-Path $settings)) { Fail "The uninstaller removed the settings $settings" }
    Write-Host "Uninstalled; the profile's settings stay ($settings)"

    foreach ($old in $ProfileDirs + @($work, $AumidKey)) { if (Test-Path $old) { Remove-Item -Recurse -Force $old } }
}

Info 'Done'
