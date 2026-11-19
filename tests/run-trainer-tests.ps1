[CmdletBinding()]
param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$out = Join-Path $build 'tests\interop'
& cmake -S $root -B $build -G 'Visual Studio 17 2022' -A Win32 -DETH_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'Trainer test configure failed.' }
& cmake --build $build --config $Configuration --target eth_trainer_value_tests eth_trainer_equipment_tests eth_trainer_lua_tests eth_trainer_movement_tests eth_mission_timer_tests eth_player_tracker_tests --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Trainer test build failed.' }
foreach ($test in @('eth_trainer_value_tests','eth_trainer_equipment_tests','eth_trainer_movement_tests','eth_mission_timer_tests','eth_player_tracker_tests')) {
    & (Join-Path $out "$test.exe")
    if ($LASTEXITCODE -ne 0) { throw "$test failed." }
}
& (Join-Path $out 'eth_trainer_value_tests.exe') unsupported
if ($LASTEXITCODE -ne 0) { throw 'Unsupported executable refusal failed.' }
foreach ($script in @('trainer-values.lua','trainer-experiments.lua')) {
    & (Join-Path $out 'eth_trainer_lua_tests.exe') (Join-Path $PSScriptRoot $script) (Join-Path $root 'mods\trainer\mod.lua')
    if ($LASTEXITCODE -ne 0) { throw "$script failed." }
}
& python (Join-Path $PSScriptRoot 'check-abi.py') $root
if ($LASTEXITCODE -ne 0) { throw 'Export ABI regression failed.' }
Write-Output 'PASS focused offline trainer suite (no installed-game access)'
