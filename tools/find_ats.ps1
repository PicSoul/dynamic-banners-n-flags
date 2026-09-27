# Prints American Truck Simulator's bin\win_x64 folder, found through Steam's registry entry and library list.
# Prints nothing if it cannot be found.
$roots = @()
foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam') {
    $item = Get-ItemProperty $key -ErrorAction SilentlyContinue
    if ($item.SteamPath) { $roots += $item.SteamPath }
    if ($item.InstallPath) { $roots += $item.InstallPath }
}
$roots += 'C:\Program Files (x86)\Steam'

$libraries = @()
foreach ($root in $roots) {
    $libraries += $root
    $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
            $libraries += $m.Groups[1].Value -replace '\\\\', '\'
        }
    }
}

foreach ($lib in $libraries) {
    $dir = Join-Path $lib 'steamapps\common\American Truck Simulator\bin\win_x64'
    if (Test-Path (Join-Path $dir 'amtrucks.exe')) { $dir; break }
}
