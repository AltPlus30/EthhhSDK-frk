# Return only explicitly declared payload files. Shared by Lua staging/install.
function Get-ModPayloadPaths {
    param([string]$ModDir, $Manifest)
    $entry = if ($Manifest.entry) { [string]$Manifest.entry } else { 'Mod.dll' }
    $files = @($entry)
    if ($null -ne $Manifest.assets) { $files += @($Manifest.assets) }
    $root = [IO.Path]::GetFullPath($ModDir).TrimEnd('\') + '\'
    foreach ($relative in ($files | Select-Object -Unique)) {
        if ($relative -isnot [string] -or [string]::IsNullOrWhiteSpace($relative) -or
            [IO.Path]::IsPathRooted($relative) -or $relative.Contains(':')) {
            throw "Invalid mod payload path: $relative"
        }
        $path = [IO.Path]::GetFullPath((Join-Path $ModDir $relative))
        if (-not $path.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing or out-of-folder mod payload: $relative"
        }
        if ([IO.Path]::GetFileName($path) -ieq 'mod.json') {
            throw 'mod.json is not a payload; existing user manifests must be preserved'
        }
        $item = Get-Item -LiteralPath $path
        while ($item -and $item.FullName.Length -ge $root.TrimEnd('\').Length) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Linked mod payload paths are not supported: $relative"
            }
            $item = if ($item -is [IO.FileInfo]) { $item.Directory } else { $item.Parent }
        }
        # Normalized relative path also prevents escaping the destination root.
        $path.Substring($root.Length)
    }
}
