# Runs on the unpacking worker, with no checkout, Python installation, or Qt setup.
param(
    [Parameter(Mandatory)][string] $Zip,
    [Parameter(Mandatory)][string] $OutputDirectory
)
$ErrorActionPreference = 'Stop'
$Zip = (Resolve-Path -LiteralPath $Zip).Path
$ev = New-Item -ItemType Directory -Force $OutputDirectory
$work = New-Item -ItemType Directory (Join-Path $env:RUNNER_TEMP "Brush Studio Review 03 $([Guid]::NewGuid().ToString('N').Substring(0, 8))")
$unpacked = Join-Path $work 'unpacked'
$cwd = Join-Path $work 'empty working directory'
$scratch = Join-Path $work 'scratch'
New-Item -ItemType Directory -Force $cwd, $scratch | Out-Null
$sha256 = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
Expand-Archive -LiteralPath $Zip -DestinationPath $unpacked
$exe = Get-Item -LiteralPath (Join-Path $unpacked 'KomaPaint Brush Studio Review 03\KomaPaint Brush Studio Review 03.exe')
$bundle = $exe.Directory.FullName + '\'

# Inspect the genuine output, not the architecture of the build shell.
$reader = [IO.BinaryReader]::new([IO.File]::OpenRead($exe.FullName))
try {
    if ($reader.ReadUInt16() -ne 0x5a4d) { throw 'Bad MZ header' }
    $reader.BaseStream.Position = 0x3c
    $pe = $reader.ReadUInt32()
    $reader.BaseStream.Position = $pe
    if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) { throw 'Not a PE AMD64 executable' }
    $reader.BaseStream.Position = $pe + 24
    if ($reader.ReadUInt16() -ne 0x020b) { throw 'Not a PE32+ executable' }
    $reader.BaseStream.Position = $pe + 24 + 68
    if ($reader.ReadUInt16() -ne 2) { throw 'Not a Windows GUI executable' }
} finally {
    $reader.Close()
}

# A hosted image contains dev tools; make them undiscoverable and check the
# process' actual loaded-module origins independently of PATH.
Get-ChildItem Env: | Where-Object { $_.Name -match '^(QT|QML|QSG|PYTHON|PYINSTALLER|PIP|CONDA|VIRTUAL_ENV|VCPKG|VCINSTALLDIR|VCToolsInstallDir|CMAKE_PREFIX_PATH|MINGW|MSYSTEM|_PYI|__PYVENV)' } |
    ForEach-Object { Remove-Item "Env:$($_.Name)" }
$systemDirectories = @("$env:SystemRoot\System32", $env:SystemRoot, "$env:SystemRoot\System32\Wbem")
$env:Path = $systemDirectories -join ';'
$env:TEMP = $scratch
$env:TMP = $scratch
Push-Location -LiteralPath $cwd
try {
    foreach ($sdk in 'python.exe','qmake.exe','qml.exe') {
        if (Get-Command $sdk -ErrorAction SilentlyContinue) { throw "Development executable still discoverable: $sdk" }
    }

    Write-Host "Testing ZIP SHA-256: $sha256"
    Write-Host 'Starting the extracted GUI as-is, with no arguments.'
    $env:KOMAPAINT_STUDIO_TRACE = Join-Path $work 'initial-launch.log'
    $app = Start-Process -FilePath $exe.FullName -WorkingDirectory $cwd -PassThru
    try {
        $wait = [Diagnostics.Stopwatch]::StartNew()
        do {
            Start-Sleep -Milliseconds 250
            $app.Refresh()
        } until ($app.HasExited -or $app.MainWindowHandle -ne [IntPtr]::Zero -or $wait.Elapsed.TotalSeconds -ge 60)
        $app.Refresh()
        if ($app.HasExited -or $app.MainWindowHandle -eq [IntPtr]::Zero -or
            $app.MainWindowTitle -notlike 'KomaPaint*Brush Studio*Third Design Review*') {
            $trace = if (Test-Path -LiteralPath $env:KOMAPAINT_STUDIO_TRACE) { Get-Content -LiteralPath $env:KOMAPAINT_STUDIO_TRACE -Raw } else { '(no app log)' }
            throw "No Brush Studio Review 03 window appeared. Title: $($app.MainWindowTitle); exited: $($app.HasExited). $trace"
        }
        $title = $app.MainWindowTitle
        $launchSeconds = [math]::Round($wait.Elapsed.TotalSeconds, 2)
        $modules = @($app.Modules | ForEach-Object { $_.FileName })
        $os = $env:SystemRoot.TrimEnd('\') + '\'
        $foreign = @($modules | Where-Object { -not $_.StartsWith($bundle, 'OrdinalIgnoreCase') -and -not $_.StartsWith($os, 'OrdinalIgnoreCase') })
        if ($foreign.Count) { throw "Loaded modules from outside the ZIP and Windows: $($foreign -join '; ')" }
        $runtime = @($modules | Where-Object {
            (Split-Path -Leaf $_) -match '^(python.*\.dll|qt6.*\.dll|qwindows\.dll|.*(?:pyside|shiboken).*\.dll|msvcp140.*\.dll|vcruntime140.*\.dll|concrt140\.dll)$'
        })
        $externalRuntime = @($runtime | Where-Object { -not $_.StartsWith($bundle, 'OrdinalIgnoreCase') })
        if ($externalRuntime.Count) { throw "Used an external dependency: $($externalRuntime -join '; ')" }
        foreach ($library in 'python312.dll','Qt6Core.dll','Qt6Qml.dll','Qt6Quick.dll','qwindows.dll','msvcp140.dll','vcruntime140.dll') {
            if ($library -notin @($runtime | ForEach-Object { Split-Path -Leaf $_ })) { throw "Running app did not load the expected bundled $library" }
        }
        if (-not $app.CloseMainWindow() -or -not $app.WaitForExit(15000) -or $app.ExitCode -ne 0) {
            throw 'Normal GUI launch did not close cleanly.'
        }
        Write-Host "Normal native GUI launch + orderly close passed ($launchSeconds s to window). All app runtimes are from the ZIP."
    } finally {
        if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force }
        Remove-Item Env:KOMAPAINT_STUDIO_TRACE
    }

    # The same Windows EXE runs its interactive check. Only the test uses a fixed DPI.
    $env:QT_SCALE_FACTOR = '1'
    $env:QT_ENABLE_HIGHDPI_SCALING = '0'
    $report = Join-Path $work 'review results'
    $walk = Start-Process -FilePath $exe.FullName -ArgumentList "--verify-package `"$report`"" -WorkingDirectory $cwd -PassThru
    if (-not $walk.WaitForExit(180000)) {
        Stop-Process -Id $walk.Id -Force
        throw 'Extracted application interaction walkthrough timed out.'
    }
    $resultPath = Join-Path $report 'qt-walkthrough.json'
    if (-not (Test-Path -LiteralPath $resultPath)) { throw "No walkthrough evidence generated (exit $($walk.ExitCode))." }
    $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    $failedChecks = @($result.checks | Where-Object { -not $_.passed })
    if ($walk.ExitCode -ne 0 -or -not $result.passed -or -not $result.frozen -or
        $result.qtPlatform -ne 'windows' -or $result.architecture -ne 64 -or
        $result.qtWarnings.Count -ne 0 -or $failedChecks.Count -ne 0 -or
        @($result.checks).Count -lt 30) {
        throw "Packaged Windows interaction checks failed: $(Get-Content -LiteralPath $resultPath -Raw)"
    }
    $review = Join-Path $report 'screenshots'
    $images = @(Get-ChildItem -LiteralPath $review -Filter '*.png' -File)
    if ($images.Count -lt 24 -or @($images | Where-Object Length -lt 10000).Count) { throw 'The full-size Qt state screenshots were not captured.' }
    foreach ($image in $images) {
        $head = [byte[]]::new(24)
        $f = [IO.File]::OpenRead($image.FullName)
        try { $n = $f.Read($head, 0, 24) } finally { $f.Close() }
        if ($n -ne 24 -or [Convert]::ToHexString([byte[]] $head[0..7]) -ne '89504E470D0A1A0A') { throw "Bad PNG: $($image.Name)" }
        $width = [Convert]::ToInt32([Convert]::ToHexString([byte[]] $head[16..19]), 16)
        $height = [Convert]::ToInt32([Convert]::ToHexString([byte[]] $head[20..23]), 16)
        if ($width -lt 960 -or $height -lt 690) { throw "Unexpected screenshot size: $width x $height" }
    }

    $finalZip = Join-Path $ev.FullName (Split-Path -Leaf $Zip)
    Copy-Item -LiteralPath $Zip -Destination $finalZip
    if ((Get-FileHash -LiteralPath $finalZip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha256) { throw 'The verified ZIP changed during handoff.' }
    Copy-Item -LiteralPath $review -Recurse -Destination (Join-Path $ev.FullName 'screenshots')
    "$sha256  $(Split-Path -Leaf $finalZip)" | Set-Content -LiteralPath (Join-Path $ev.FullName 'SHA256SUMS.txt') -Encoding utf8
    [ordered]@{
        passed = $true
        zip = (Split-Path -Leaf $finalZip)
        sha256 = $sha256
        bytes = (Get-Item $finalZip).Length
        commit = $env:GITHUB_SHA
        ciRun = $env:GITHUB_RUN_ID
        os = [System.Environment]::OSVersion.VersionString
        runnerImage = $env:ImageOS
        architecture = 'AMD64 / PE32+ / GUI'
        launchedWithoutArguments = $true
        windowTitle = $title
        secondsToWindow = $launchSeconds
        cleanExit = $true
        systemOnlyPath = $env:Path
        externalDevelopmentModules = @()
        bundledRuntimeModules = @($runtime | ForEach-Object { $_.Substring($bundle.Length) } | Sort-Object)
        screenshots = $images.Count
        walkthrough = $result
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $ev.FullName 'verification.json') -Encoding utf8
    @('### Extracted Windows package passed', "- SHA-256: $sha256",
      '- GUI startup without arguments, clean shutdown, and app-local DLL verification.',
      "- $($images.Count) Qt states and $($result.checks.Count) interaction checks passed on the native Windows platform.") |
      Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY
    Write-Host "PASS: extracted x64 package, $($result.checks.Count) interaction checks, and $($images.Count) screenshots."
} finally {
    Pop-Location
}
