#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$ExpectedSha256,
    [switch]$NativeUi
)

$ErrorActionPreference = 'Stop'
$archive = (Resolve-Path -LiteralPath $Package).ProviderPath
$filename = [IO.Path]::GetFileName($archive)
if ($filename -cnotmatch '^ARGUS-(\d+\.\d+\.\d+)-windows-x64\.zip$') {
    throw 'Unexpected package filename.'
}
$version = $Matches[1]
if ($ExpectedSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
    (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $ExpectedSha256) {
    throw 'Package SHA-256 verification failed.'
}
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    $entryNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [long]$bytes = 0
    foreach ($entry in $zip.Entries) {
        $bytes += $entry.Length
        if ($bytes -gt 128MB -or $entryNames.Count -ge 1024 -or
            $entry.FullName -cnotmatch '^[A-Za-z0-9_#-][A-Za-z0-9_#. /-]*$' -or
            ($entry.FullName.Split('/') | Where-Object { $_ -in '.', '..' -or $_.EndsWith('.') -or $_.EndsWith(' ') }) -or
            (($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000 -or
            !$entryNames.Add($entry.FullName)) {
            throw "Invalid portable archive entry: $($entry.FullName)"
        }
    }
} finally {
    $zip.Dispose()
}

$temp = (Resolve-Path -LiteralPath ([IO.Path]::GetTempPath())).ProviderPath.TrimEnd('\')
$workspace = Join-Path $temp "ARGUS portable test $([Guid]::NewGuid())"
$data = Join-Path $workspace 'Operator data'
$desktopData = Join-Path $workspace 'Desktop run'
$fleetData = Join-Path $workspace 'Fleet reports'
$trafficData = Join-Path $workspace 'Shared aisle'
$first = Join-Path $workspace 'Program files'
$replacement = Join-Path $workspace 'Updated program files'
$deny = Join-Path $workspace 'Read only'
$bin = Join-Path $first 'bin'
[void][IO.Directory]::CreateDirectory($workspace)

function Remove-TestFolder([string]$Folder) {
    $resolved = (Resolve-Path -LiteralPath $Folder).ProviderPath
    if (!$resolved.StartsWith("$temp\ARGUS portable test ", [StringComparison]::OrdinalIgnoreCase) -or
        ($resolved -ne $workspace -and !$resolved.StartsWith("$workspace\", [StringComparison]::OrdinalIgnoreCase))) {
        throw 'Refusing to remove a directory outside the isolated test workspace.'
    }
    for ($attempt = 1; $attempt -le 12; ++$attempt) {
        try {
            Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction Stop
            return
        } catch [System.IO.IOException] {
            if ($attempt -eq 12) { throw }
            Start-Sleep -Milliseconds 250
        }
    }
}

function Assert-Package([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}

function Invoke-Portable {
    param([string]$Name, [string[]]$Arguments, [int]$Exit = 0, [string]$InputFile)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $bin "$Name.exe"
    $start.WorkingDirectory = $data
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.RedirectStandardInput = [bool]$InputFile
    $start.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($arg in $Arguments) { [void]$start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $output = $process.StandardOutput.ReadToEndAsync()
        $diagnostics = $process.StandardError.ReadToEndAsync()
        if ($InputFile) {
            $file = [IO.File]::OpenRead($InputFile)
            try { $file.CopyTo($process.StandardInput.BaseStream) }
            finally { $file.Dispose(); $process.StandardInput.Close() }
        }
        if (!$process.WaitForExit(70000)) {
            $process.Kill($true)
            throw "$Name exceeded the acceptance timeout."
        }
        if ($process.ExitCode -ne $Exit) {
            throw "$Name exit $($process.ExitCode), expected ${Exit}. $($diagnostics.GetAwaiter().GetResult())"
        }
        $output.GetAwaiter().GetResult().Trim()
    } finally {
        $process.Dispose()
    }
}

try {
    [void][IO.Directory]::CreateDirectory($data)
    Expand-Archive -LiteralPath $archive -DestinationPath $first
    $proof = Get-Content -LiteralPath (Join-Path $first 'verification/build.json') -Raw | ConvertFrom-Json
    $results = Get-Content -LiteralPath (Join-Path $first 'verification/ctest.json') -Raw | ConvertFrom-Json
    Assert-Package ($proof.version -ceq $version -and $proof.signing -ceq 'unsigned' -and
        $proof.sourceCommit -cmatch '^[0-9a-f]{40}$' -and $results.count -ge 100 -and
        $results.count -eq @($results.tests | Where-Object result -eq 'passed').Count) 'Invalid build evidence.'
    foreach ($notice in @('NOTICE.md', 'licenses/sdl3/copyright', 'licenses/sqlite3/copyright',
            'verification/sbom/SHA256SUMS', 'docs/release/onboarding.md',
            'verification/test-windows-package.ps1',
            'docs/testing/evidence/phase-14-saved-run.png')) {
        Assert-Package (Test-Path -LiteralPath (Join-Path $first $notice) -PathType Leaf) "Missing $notice."
    }
    foreach ($name in @('argus-headless', 'argus-desktop')) {
        Assert-Package ((Invoke-Portable $name @('--version')) -ceq "ARGUS $version") "Version mismatch for $name."
    }

    $scenario = Join-Path $data 'created-warehouse.json'
    $authored = Get-Content -LiteralPath (Join-Path $first 'examples/warehouse/small-backroom.json') -Raw |
        ConvertFrom-Json
    $authored.scenarioId = 1201
    $authored | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $scenario -Encoding utf8
    $robot = Invoke-Portable 'argus-headless' @('--scenario-stdin', '--drive-steps', '220') -InputFile $scenario |
        ConvertFrom-Json
    Assert-Package ($robot.scenarioId -eq 1201 -and $robot.robot.odometry.xM -gt 13 -and
        $robot.robot.collisionBlocked) 'Custom warehouse launch or bounded robot motion failed.'
    $explore = Invoke-Portable 'argus-headless' @('--warehouse', 'small-backroom', '--explore-steps', '80') |
        ConvertFrom-Json
    Assert-Package ($explore.plans -gt 0 -and $explore.free -gt 0) 'The exploration workflow failed.'

    $map = Join-Path $data 'survey.argmap'
    $mapArgs = @('--warehouse', 'small-backroom', '--export-map', '80', '--cell-mm', '300')
    $null = Invoke-Portable 'argus-headless' ($mapArgs + @('--map-output', $data, 'survey.argmap'))
    $snapshot = [IO.File]::ReadAllBytes($map)
    $pin = [Convert]::ToHexString($snapshot, $snapshot.Length - 32, 32).ToLowerInvariant()
    $readArgs = @('--warehouse', 'small-backroom', '--map-stdin', '--cell-mm', '300', '--map-sha256', $pin)
    $mapped = Invoke-Portable 'argus-headless' $readArgs -InputFile $map | ConvertFrom-Json
    Assert-Package ($mapped.free -gt 50 -and $mapped.occupied -gt 10) 'Verified map evidence was empty.'
    $corrupt = Join-Path $data 'corrupt.argmap'
    $snapshot[37] = $snapshot[37] -bxor 1
    [IO.File]::WriteAllBytes($corrupt, $snapshot)
    Assert-Package (!(Invoke-Portable 'argus-headless' $readArgs -InputFile $corrupt -Exit 1)) 'Corrupt map was emitted.'

    $catalog = Join-Path $data 'catalog.db'
    $missions = Join-Path $data 'missions.db'
    $catalogExample = Join-Path $first 'examples/inventory'
    $null = Invoke-Portable 'argus-inventory' @($catalog, 'import', $catalogExample, 'fictional-catalog.csv')
    $lookup = Invoke-Portable 'argus-inventory' @($catalog, 'find', '990-01-0001') | ConvertFrom-Json
    Assert-Package ($lookup.units.Count -eq 2) 'Catalog lookup differs from the seeded example.'
    $find = Invoke-Portable 'argus-inventory' @($catalog, 'find-item', '990-01-0001', '801', 'TECH') |
        ConvertFrom-Json
    Assert-Package ($find.items.Count -eq 2 -and $find.visits[0].aisle -ceq 'TECH') 'Targeted search failed.'
    $json = Join-Path $data 'audit.json'
    $csv = Join-Path $data 'review.csv'
    Invoke-Portable 'argus-inventory' @($catalog, 'audit', 'SOFTLINES', '802', 'json') |
        Set-Content -LiteralPath $json -Encoding utf8
    Invoke-Portable 'argus-inventory' @($catalog, 'audit', 'SOFTLINES', '803', 'csv') |
        Set-Content -LiteralPath $csv -Encoding utf8
    $report = Get-Content -LiteralPath $json -Raw | ConvertFrom-Json
    $cases = @(Import-Csv -LiteralPath $csv)
    Assert-Package ($report.quantities[0].expectedQuantity -eq 4 -and
        $report.quantities[0].uniqueObservedInAisle -eq 1 -and
        @($cases | Where-Object finding -eq 'likely_misplaced').Count -eq 1) 'Audit or CSV review results differ.'
    $null = Invoke-Portable 'argus-mission' @($missions, 'submit', '901', 'scan-all', 'routine')
    $mission = Invoke-Portable 'argus-mission' @($missions, 'run', '1600', '-') | ConvertFrom-Json
    Assert-Package ($mission.state -ceq 'completed' -and $mission.coveredViews -eq 4) 'Mission did not complete.'

    [void][IO.Directory]::CreateDirectory($deny)
    $acl = Get-Acl -LiteralPath $deny
    $restricted = Get-Acl -LiteralPath $deny
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $restricted.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid, 'Write', 'Deny'))
    Set-Acl -LiteralPath $deny -AclObject $restricted
    try {
        $null = Invoke-Portable 'argus-headless' ($mapArgs + @('--map-output', $deny, 'blocked.argmap')) -Exit 1
        Assert-Package (!(Test-Path -LiteralPath (Join-Path $deny 'blocked.argmap'))) 'Denied export changed storage.'
    } finally {
        Set-Acl -LiteralPath $deny -AclObject $acl
    }

    $ui = $null
    $uiReopen = $null
    $fleet = $null
    $fleetReopen = $null
    $review = $null
    $reviewReopen = $null
    $traffic = $null
    if ($NativeUi) {
        [void][IO.Directory]::CreateDirectory($desktopData)
        $ui = Invoke-Portable 'argus-desktop' @('--ui-smoke', $desktopData) | ConvertFrom-Json
        $tourPassed = $ui.workflowTour -and $ui.movingObserved -and $ui.truthIsolated -and
            $ui.lidarDropoutHeld -and $ui.lidarRecovered -and $ui.continuousRun -and
            $ui.savedRunReopened -and $ui.resized -and $ui.contextRebuilt -and
            $ui.occupancyBuilds -gt 0 -and $ui.occupancyBuilds -lt $ui.occupancyFrames
        Assert-Package $tourPassed 'Installed native workflow tour failed.'
        [void][IO.Directory]::CreateDirectory($fleetData)
        [void][IO.Directory]::CreateDirectory($trafficData)
        $fleet = Invoke-Portable 'argus-desktop' @('--ui-fleet-smoke', $fleetData) | ConvertFrom-Json
        Assert-Package ($fleet.fleetAudits -and $fleet.mixedLocations -and $fleet.maintenanceBothRobots -and
            $fleet.auditableLocations -eq 21) 'Installed native fleet workflow failed.'
        $review = Invoke-Portable 'argus-desktop' @('--ui-fleet-review', $fleetData) | ConvertFrom-Json
        Assert-Package ($review.reviewed -and $review.reviewFilter -and $review.recheckLinked -and
            $review.comparisonVisible) 'Installed audit review workflow failed.'
        $traffic = Invoke-Portable 'argus-desktop' @('--ui-fleet-traffic', $trafficData) | ConvertFrom-Json
        Assert-Package ($traffic.sharedFleet -and $traffic.yielded -and
            $traffic.bothAuditsCompleted -and $traffic.secondLocation -ceq 'A20J07') 'Installed shared-aisle workflow failed.'
    }

    Expand-Archive -LiteralPath $archive -DestinationPath $replacement
    Remove-TestFolder $first
    $bin = Join-Path $replacement 'bin'
    $restored = Invoke-Portable 'argus-mission' @($missions, 'report', '901') | ConvertFrom-Json
    $reopened = Invoke-Portable 'argus-headless' $readArgs -InputFile $map | ConvertFrom-Json
    Assert-Package ($restored.state -ceq 'completed' -and $restored.coveredViews -eq $mission.coveredViews -and
        $reopened.free -eq $mapped.free -and (Test-Path -LiteralPath $json) -and
        (Test-Path -LiteralPath $csv)) 'Portable replacement did not preserve the run and reports.'
    if ($NativeUi) {
        $uiReopen = Invoke-Portable 'argus-desktop' @('--ui-reopen', $desktopData) | ConvertFrom-Json
        Assert-Package $uiReopen.savedRunReopenedAfterRestart 'Portable replacement did not reopen the saved desktop run.'
        $fleetReopen = Invoke-Portable 'argus-desktop' @('--ui-fleet-reopen', $fleetData) | ConvertFrom-Json
        Assert-Package ($fleetReopen.reopenedAfterRestart -and $fleetReopen.dashboard1280x720 -and
            $fleetReopen.returnedToBerth -and $fleetReopen.auditSavedAfterHold) 'Portable replacement could not recover the fleet and preserve audit evidence through an interrupted return.'
        $reviewReopen = Invoke-Portable 'argus-desktop' @('--ui-fleet-review-reopen', $fleetData) | ConvertFrom-Json
        Assert-Package ($reviewReopen.reopened -and $reviewReopen.reviewed -and $reviewReopen.recheckLinked -and
            $reviewReopen.comparisonVisible) 'Portable replacement could not reopen the reviewed report and re-audit.'
    }
    Remove-TestFolder $replacement
    Assert-Package (!(Test-Path -LiteralPath $replacement) -and (Test-Path -LiteralPath $catalog) -and
        (Test-Path -LiteralPath $missions) -and (Test-Path -LiteralPath $map)) 'Portable uninstall changed operator data.'
    if ($NativeUi) {
        $desktopDataPreserved = (Test-Path -LiteralPath (Join-Path $desktopData 'catalog.db') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $desktopData 'missions.db') -PathType Leaf)
        Assert-Package $desktopDataPreserved 'Portable uninstall removed the saved desktop databases.'
        Assert-Package ((Test-Path -LiteralPath (Join-Path $fleetData 'missions.db') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $trafficData 'missions.db') -PathType Leaf)) 'Portable uninstall removed the fleet journals.'
    }
    $checks = @('digest', 'extract', 'version', 'custom-warehouse', 'robot-deployment', 'exploration',
        'map-pin-and-corruption', 'product-lookup', 'item-search', 'audit-and-csv-review', 'mission',
        'denied-write', 'replacement-and-reopen', 'portable-uninstall')
    if ($NativeUi) {
        $checks += @('native-continuous-workflow', 'native-reopen-after-replacement', 'native-data-after-uninstall')
        $checks += @('native-fleet-audits', 'native-audit-review', 'native-shared-aisle',
            'native-fleet-recovery-after-replacement', 'native-review-after-replacement')
    }
    $graphics = if ($NativeUi) {
        @(Get-CimInstance -ClassName Win32_VideoController -ErrorAction SilentlyContinue |
            Select-Object Name, DriverVersion)
    }
    [ordered]@{
        package = $filename
        sha256 = $ExpectedSha256.ToLowerInvariant()
        sourceCommit = $proof.sourceCommit
        windowsVersion = [Environment]::OSVersion.Version.ToString()
        graphicsDrivers = @($graphics | Where-Object { $_ })
        nativeUi = $ui
        nativeReopen = $uiReopen
        nativeFleet = $fleet
        nativeFleetReopen = $fleetReopen
        nativeReview = $review
        nativeReviewReopen = $reviewReopen
        nativeTraffic = $traffic
        checks = $checks
    } | ConvertTo-Json -Depth 5
} finally {
    Remove-TestFolder $workspace
}
