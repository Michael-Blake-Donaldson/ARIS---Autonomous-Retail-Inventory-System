# ARGUS Windows quick start

## Install, update, and remove

Use Windows 11 x64 with an interactive desktop and a graphics driver that supports a Windows OpenGL compatibility context. The package includes the app-local Visual C++ runtime. The command-line programs can run without an interactive graphics session. Use an ordinary Windows account with permission to write its own data folder. The recommended test configuration has a 1280 × 720 display or larger.

In PowerShell, compare the ZIP digest to the expected SHA-256 from a trusted release channel, then extract to a new directory. These examples use version 0.1.0.

```powershell
Get-FileHash -Algorithm SHA256 .\ARGUS-0.1.0-windows-x64.zip
Expand-Archive .\ARGUS-0.1.0-windows-x64.zip -DestinationPath .\ARGUS-0.1.0
Set-Location .\ARGUS-0.1.0
.\bin\argus-desktop.exe
```

The ARGUS executables are unsigned. Windows may show an unknown-publisher warning. Verify the package against the trusted digest and follow your organization's execution policy. Keep the DLLs beside the executables.

The default desktop session uses temporary fictional databases. For durable tools and exports, select a local folder under your profile. Close the app before backing up its databases and their journal files. Restore a consistent backup as a unit.

```powershell
$data = Join-Path $env:LOCALAPPDATA 'ARGUS'
New-Item -ItemType Directory -Force $data | Out-Null
.\bin\argus-desktop.exe --operations-data $data
```

The persistent desktop uses `catalog.db` and `missions.db` inside that directory. Keep the program and data folders separate. For an update, extract the new version to a new program directory, back up the data, then use the new executables with the same data paths. Remove a portable installation by closing ARGUS and deleting its extracted program directory. Data in the external folder is preserved.

## Drive, map, and explore

1. Open the desktop and select **Overview** from Fleet. Hold Space and use the arrow keys to drive. Release Space to brake.
2. Use F4 to show LiDAR, F5 for estimated occupancy, and **Explore** for observation-driven navigation.
3. Use F3 and the labeled Diagnostics checkbox to see simulator truth briefly. Return to Overview for the operational view.
4. For a custom scenario, copy `examples/warehouse/small-backroom.json` to your data directory and change its positive `scenarioId`. The schema and limits are in [Warehouse scenarios](../warehouse.md).

From Command Prompt, run the following in the extracted program directory. Use Command Prompt for binary or redirected input.

```bat
mkdir "%LOCALAPPDATA%\ARGUS"
bin\argus-headless.exe --scenario-stdin --drive-steps 220 < "%LOCALAPPDATA%\ARGUS\custom.json"
bin\argus-headless.exe --warehouse small-backroom --explore-steps 80
bin\argus-headless.exe --warehouse small-backroom --export-map 80 --map-output "%LOCALAPPDATA%\ARGUS" survey.argmap
bin\argus-headless.exe --warehouse small-backroom --map-stdin < "%LOCALAPPDATA%\ARGUS\survey.argmap"
```

A matching warehouse fixture and cell size are required on map import. Each loaded version 2 snapshot verifies its internal SHA-256. A digest obtained independently can pin the export with `--map-sha256`. [Map format and pinning](../mapping.md)

## Search, audit, and reopen

For shelf-level audits, start both assigned shelves from **Fleet**, monitor sensors in **Robot Maintenance**, and review product-level results in **Location Audits**. Use the same `--operations-data` directory when you restart to reopen those reports. [Two-robot walkthrough](../operations-center.md#run-a-two-robot-shelf-audit)

In the default `rfid-audit` stockroom, Explore, product investigation, audit and routed missions share the same robot and map. Use **Find Product** for fictional DPCI `990-01-0001`, **Inventory Audit** to audit `SOFTLINES`, and **Missions** to scan aisles or return to dock. The audit shows expected quantities, unique observed EPCs and findings for human review.

After an audit, open **Lineage / History** and choose **Save map and audit**. Close ARGUS, start it again with the same `--operations-data` directory, and use **Open saved run** in that panel. **Copy audit JSON** copies the archived report. **Return to live** returns to the new session. Each save replaces the previous capture in that data folder.

To export files and reproduce the work using separate CLI databases, run these commands once from the extracted program directory in Command Prompt. Choose new positive session and mission IDs for subsequent runs.

```bat
set "ARGUS_DATA=%LOCALAPPDATA%\ARGUS"
bin\argus-inventory.exe "%ARGUS_DATA%\cli-catalog.db" import examples\inventory fictional-catalog.csv
bin\argus-inventory.exe "%ARGUS_DATA%\cli-catalog.db" find-item 990-01-0001 801 TECH
bin\argus-inventory.exe "%ARGUS_DATA%\cli-catalog.db" audit SOFTLINES 802 json > "%ARGUS_DATA%\audit.json"
bin\argus-inventory.exe "%ARGUS_DATA%\cli-catalog.db" audit SOFTLINES 803 csv > "%ARGUS_DATA%\review.csv"
bin\argus-mission.exe "%ARGUS_DATA%\cli-missions.db" submit 901 scan-all routine
bin\argus-mission.exe "%ARGUS_DATA%\cli-missions.db" run 1600 -
bin\argus-mission.exe "%ARGUS_DATA%\cli-missions.db" report 901
```

Reopen `audit.json` in a text or JSON viewer, and `review.csv` in a spreadsheet or text viewer. The aisle report separates expected, assigned, and uniquely observed quantities. Verify discrepancies independently before treating stock as missing. [Audit findings and review](../audit.md)

| Shortcut | Effect |
| --- | --- |
| Space + arrow keys | Manual motion with deadman held |
| X, R | Latch stop, or clear it with Space released |
| Home, Escape | Restart the live simulation, or close the window |
| 1, 2, 3 | Orbit, top-down, follow cameras |
| F3, F4, F5 | Diagnostics, LiDAR, estimated map |

## Troubleshooting

| Symptom | Action |
| --- | --- |
| DLL startup error | Verify the ZIP and extract the full package to a new folder. Keep `bin` together. |
| Graphics context error | Use an interactive Windows session and the supported driver. Run the CLI for graphics-independent workflows. |
| `database_failure`, `storage`, or `busy` | Close other writers, verify local folder permissions and free space, and preserve damaged data before restoring a trusted backup. |
| `incompatible_map` | Select the original fixture and resolution and confirm any independently obtained pin. |
| Audit session already exists | Reserve a fresh positive session ID. Keep earlier evidence for comparison. |
| Work interrupted | Reopen the original mission store and follow its recovery and relocalization flow. [Mission recovery](../missions.md) |
