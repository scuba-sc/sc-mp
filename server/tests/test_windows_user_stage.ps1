# SPDX-License-Identifier: GPL-3.0-only
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Equal([string]$Expected, [string]$Actual, [string]$Message) {
    if ($Expected -cne $Actual) {
        throw $Message
    }
}

function Invoke-Stage([string]$Script, [string]$SourceExecutable, [string]$Destination) {
    return (& $Script -RelayExecutable $SourceExecutable -InstallDirectory $Destination *>&1 | Out-String)
}

$serverRoot = Split-Path -Parent $PSScriptRoot
$stageScript = Join-Path $serverRoot 'deploy\windows\stage_user_relay.ps1'
$exampleConfig = Join-Path $serverRoot 'sco-relay.example.conf'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("sc-mp-user-stage-" + [Guid]::NewGuid().ToString('N'))
$sourceDirectory = Join-Path $testRoot 'built'
$stageDirectory = Join-Path $testRoot 'selected'
$rollbackDirectory = Join-Path $testRoot 'rollback'
$preserveDirectory = Join-Path $testRoot 'failed-update'

try {
    New-Item -ItemType Directory -Path $sourceDirectory | Out-Null
    New-Item -ItemType Directory -Path $stageDirectory | Out-Null
    New-Item -ItemType Directory -Path $rollbackDirectory | Out-Null
    New-Item -ItemType Directory -Path $preserveDirectory | Out-Null

    $sourceExecutable = Join-Path $sourceDirectory 'sco-relay.exe'
    $stagedExecutable = Join-Path $stageDirectory 'sco-relay.exe'
    $stagedConfig = Join-Path $stageDirectory 'sco-relay.conf'
    [IO.File]::WriteAllText($sourceExecutable, 'synthetic relay build v1')

    $firstStageOutput = Invoke-Stage $stageScript $sourceExecutable $stageDirectory
    $actualExecutable = [IO.File]::ReadAllText($stagedExecutable)
    $actualConfig = [IO.File]::ReadAllText($stagedConfig)
    Assert-Equal 'synthetic relay build v1' $actualExecutable 'First stage did not copy the selected executable.'
    Assert-Equal ([IO.File]::ReadAllText($exampleConfig)) $actualConfig 'First stage did not copy the example config.'
    $placeholderWasPrinted = $firstStageOutput.Contains('CHANGE-ME-to-a-long-secret-sentence')
    Assert-True (-not $placeholderWasPrinted) 'First-stage output exposed the example passphrase placeholder.'

    $operatorConfig = "# synthetic user-owned config`r`npassphrase = test-only-placeholder`r`nmax_players = 3`r`n"
    [IO.File]::WriteAllText($stagedConfig, $operatorConfig)
    [IO.File]::WriteAllText($sourceExecutable, 'synthetic relay build v2')
    $updateOutput = Invoke-Stage $stageScript $sourceExecutable $stageDirectory
    $actualExecutable = [IO.File]::ReadAllText($stagedExecutable)
    $actualConfig = [IO.File]::ReadAllText($stagedConfig)
    Assert-Equal 'synthetic relay build v2' $actualExecutable 'Update did not replace the staged executable.'
    Assert-Equal $operatorConfig $actualConfig 'Update changed the existing user config.'
    Assert-True (-not $updateOutput.Contains('test-only-placeholder')) 'Update output exposed existing config content.'
    $remainingFiles = (Get-ChildItem -LiteralPath $stageDirectory -Force | Measure-Object).Count
    Assert-True ($remainingFiles -eq 2) 'Successful update left temporary files behind.'

    $rollbackExecutable = Join-Path $rollbackDirectory 'sco-relay.exe'
    $rollbackConfig = Join-Path $rollbackDirectory 'sco-relay.conf'
    [IO.File]::WriteAllText($rollbackExecutable, 'synthetic previous executable')
    $lockedExecutable = [IO.File]::Open(
        $rollbackExecutable, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $stageFailed = $false
    try {
        try {
            $null = Invoke-Stage $stageScript $sourceExecutable $rollbackDirectory
        }
        catch {
            $stageFailed = $true
        }
    }
    finally {
        $lockedExecutable.Dispose()
    }
    Assert-True $stageFailed 'Staging unexpectedly replaced an executable locked by another process.'
    $actualExecutable = [IO.File]::ReadAllText($rollbackExecutable)
    Assert-Equal 'synthetic previous executable' $actualExecutable 'Failed update changed the previous executable.'
    Assert-True (-not (Test-Path -LiteralPath $rollbackConfig)) 'Failed first stage left a new config behind.'
    $remainingFiles = (Get-ChildItem -LiteralPath $rollbackDirectory -Force | Measure-Object).Count
    Assert-True ($remainingFiles -eq 1) 'Failed stage left temporary files behind.'

    $preservedExecutable = Join-Path $preserveDirectory 'sco-relay.exe'
    $preservedConfig = Join-Path $preserveDirectory 'sco-relay.conf'
    $operatorConfig = "# existing operator config`r`n# preserve this content`r`n"
    [IO.File]::WriteAllText($preservedExecutable, 'synthetic executable before failed update')
    [IO.File]::WriteAllText($preservedConfig, $operatorConfig)
    $lockedExecutable = [IO.File]::Open(
        $preservedExecutable, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $updateFailed = $false
    try {
        try {
            $null = Invoke-Stage $stageScript $sourceExecutable $preserveDirectory
        }
        catch {
            $updateFailed = $true
        }
    }
    finally {
        $lockedExecutable.Dispose()
    }
    Assert-True $updateFailed 'Locked executable did not reject the update.'
    $actualExecutable = [IO.File]::ReadAllText($preservedExecutable)
    $actualConfig = [IO.File]::ReadAllText($preservedConfig)
    Assert-Equal 'synthetic executable before failed update' $actualExecutable 'Failed update changed the existing executable.'
    Assert-Equal $operatorConfig $actualConfig 'Failed update changed the existing user config.'
    $remainingFiles = (Get-ChildItem -LiteralPath $preserveDirectory -Force | Measure-Object).Count
    Assert-True ($remainingFiles -eq 2) 'Failed update left temporary files behind.'

    Write-Host 'WINDOWS_USER_STAGE_PASS first stage, config-preserving update, and failure rollback'
    exit 0
}
catch {
    [Console]::Error.WriteLine("Windows per-user staging checks failed: $($_.Exception.Message)")
    exit 1
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
