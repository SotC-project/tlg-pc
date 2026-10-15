# The Last Guardian - creates (or refreshes) the Desktop and Start menu shortcuts.
# Run Install.bat. -Remove deletes them again.
param([switch]$Remove)

$exe = Join-Path $PSScriptRoot 'The Last Guardian.exe'
$icon = Join-Path $PSScriptRoot 'game.ico'
$links = @(
    (Join-Path ([Environment]::GetFolderPath('Desktop')) 'The Last Guardian.lnk'),
    (Join-Path ([Environment]::GetFolderPath('Programs')) 'The Last Guardian.lnk')
)

if ($Remove) {
    foreach ($path in $links) { if (Test-Path $path) { Remove-Item $path; Write-Host "Removed $path" } }
    exit 0
}
if (-not (Test-Path $exe)) {
    Write-Host "Program not found: $exe"
    exit 1
}
$shell = New-Object -ComObject WScript.Shell
foreach ($path in $links) {
    $link = $shell.CreateShortcut($path)
    $link.TargetPath = $exe
    # The program keeps its saves and settings in the "user" folder beside it.
    $link.WorkingDirectory = $PSScriptRoot
    $link.IconLocation = "$icon,0"
    $link.Description = 'The Last Guardian'
    $link.Save()
    Write-Host "Shortcut: $path"
}
Write-Host ""
Write-Host "To pin it to the taskbar: start the game, right-click its taskbar button, Pin to taskbar."
