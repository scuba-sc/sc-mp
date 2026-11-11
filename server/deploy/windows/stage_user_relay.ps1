# SPDX-License-Identifier: GPL-3.0-only
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$RelayExecutable,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$InstallDirectory,

    [switch]$ReplaceExisting
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Assert-RegularFile([string]$Path, [string]$Description) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw "$Description must be a regular file, not a directory or reparse point."
    }
    return $item
}

function Get-OptionalRegularFile([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    return (Assert-RegularFile $Path $Description)
}

function Assert-NoReparsePathComponents([string]$Path, [string]$Description) {
    $currentPath = [IO.Path]::GetFullPath($Path)
    $pathRoot = [IO.Path]::GetPathRoot($currentPath)
    $pathSeparators = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $pathRootKey = $pathRoot.TrimEnd($pathSeparators)
    while (-not [string]::IsNullOrEmpty($currentPath)) {
        try {
            # Hidden/system ancestors (including AppData) still require the same reparse-point check.
            $component = Get-Item -LiteralPath $currentPath -Force -ErrorAction Stop
        }
        catch {
            throw "$Description path component could not be inspected: '$currentPath'."
        }
        if (($component.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Description path contains a reparse point: '$currentPath'."
        }
        if ($currentPath.TrimEnd($pathSeparators).Equals($pathRootKey, [StringComparison]::OrdinalIgnoreCase)) {
            break
        }
        $parentPath = [IO.Path]::GetDirectoryName($currentPath)
        if ([string]::IsNullOrEmpty($parentPath) -or $parentPath -eq $currentPath) {
            break
        }
        $currentPath = $parentPath
    }
}

function Remove-TemporaryFile([string]$Path) {
    if ([string]::IsNullOrEmpty($Path) -or -not (Test-Path -LiteralPath $Path)) {
        return
    }
    try {
        [IO.File]::Delete($Path)
    }
    catch {
        Write-Warning "Could not remove temporary staging file '$Path'; inspect it before retrying."
    }
}

$source = Assert-RegularFile ([IO.Path]::GetFullPath($RelayExecutable)) 'The relay executable'
if ([IO.Path]::GetExtension($source.FullName) -ine '.exe') {
    throw 'The selected relay executable must have the .exe extension.'
}

$destination = Get-Item -LiteralPath $InstallDirectory -Force -ErrorAction Stop
if (-not $destination.PSIsContainer -or (($destination.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
    throw 'The selected install directory must be an existing directory, not a reparse point.'
}
$destinationPath = [IO.Path]::GetFullPath($destination.FullName)
Assert-NoReparsePathComponents $destinationPath 'The selected install directory'
$targetExecutable = Join-Path $destinationPath 'sco-relay.exe'
$targetConfig = Join-Path $destinationPath 'sco-relay.conf'
$exampleConfig = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\sco-relay.example.conf'))
$null = Assert-RegularFile $exampleConfig 'The example config'

$oldExecutable = Get-OptionalRegularFile $targetExecutable 'The staged executable'
$hadExecutable = $null -ne $oldExecutable
$oldConfig = Get-OptionalRegularFile $targetConfig 'The staged config'
$hadConfig = $null -ne $oldConfig
if ($hadExecutable -and -not $ReplaceExisting) {
    throw "The selected directory already contains sco-relay.exe. No files were changed; pass -ReplaceExisting only after confirming this is the relay executable to replace."
}

$token = [Guid]::NewGuid().ToString('N')
$stagedExecutable = Join-Path $destinationPath ".sco-relay.exe.stage-$token"
$stagedConfig = Join-Path $destinationPath ".sco-relay.conf.stage-$token"
$backupExecutable = Join-Path $destinationPath ".sco-relay.exe.backup-$token"
$rollbackExecutable = Join-Path $destinationPath ".sco-relay.exe.rollback-$token"
$configCreated = $false
$executableChanged = $false

try {
    # Stage beside the destination so the final rename stays on the same volume.
    [IO.File]::Copy($source.FullName, $stagedExecutable, $false)
    if (-not $hadConfig) {
        [IO.File]::Copy($exampleConfig, $stagedConfig, $false)
        # File.Move fails if another config appeared; it never overwrites an operator's file.
        [IO.File]::Move($stagedConfig, $targetConfig)
        $configCreated = $true
    }

    if ($hadExecutable) {
        # ReplaceFile is atomic on the same volume and retains the old executable for rollback.
        [IO.File]::Replace($stagedExecutable, $targetExecutable, $backupExecutable)
        $executableChanged = $true
    }
    else {
        # File.Move fails if another executable appeared; it never overwrites one unexpectedly.
        [IO.File]::Move($stagedExecutable, $targetExecutable)
        $executableChanged = $true
    }
}
catch {
    $failure = $_.Exception.Message
    $rollbackNote = ''

    # If replacement completed before a later failure, put the old executable back atomically.
    if ($hadExecutable -and (Test-Path -LiteralPath $backupExecutable -PathType Leaf)) {
        try {
            [IO.File]::Replace($backupExecutable, $targetExecutable, $rollbackExecutable)
            Remove-TemporaryFile $rollbackExecutable
            $executableChanged = $false
        }
        catch {
            $rollbackNote = " The previous executable backup remains at '$backupExecutable'."
        }
    }
    elseif (-not $hadExecutable -and $executableChanged) {
        try {
            [IO.File]::Delete($targetExecutable)
            $executableChanged = $false
        }
        catch {
            $rollbackNote = " The newly staged executable remains at '$targetExecutable'."
        }
    }

    if ($configCreated) {
        try {
            [IO.File]::Delete($targetConfig)
            $configCreated = $false
        }
        catch {
            $rollbackNote += " The newly created example config remains at '$targetConfig'."
        }
    }

    throw "Staging failed: $failure.$rollbackNote Existing user config files are never replaced."
}
finally {
    Remove-TemporaryFile $stagedExecutable
    Remove-TemporaryFile $stagedConfig
    Remove-TemporaryFile $rollbackExecutable
}

if ($hadExecutable) {
    Remove-TemporaryFile $backupExecutable
}

Write-Host "Staged relay executable: $targetExecutable"
if ($hadConfig) {
    Write-Host "Preserved existing user config: $targetConfig"
}
else {
    Write-Host "Created example config: $targetConfig. Edit its placeholder passphrase and max_players before starting the relay."
}
Write-Host 'The relay was not started. See server/deploy/windows/README.md for foreground start and clean stop steps.'
