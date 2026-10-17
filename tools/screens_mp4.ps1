<#
.SYNOPSIS
Makes docs\screens.mp4 from the pictures in docs\screens, one after the other.

.DESCRIPTION
Uses the video encoder that comes with Windows 10 and later (Windows.Media.Editing),
so there is nothing to install. The pictures are 640 x 480, which is the size of
the video. Run tools\screenshots.py first for the pictures.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File tools\screens_mp4.ps1 -Seconds 2
#>
[CmdletBinding()]
param(
    [double] $Seconds = 2.0,
    [int]    $Bitrate = 600000,
    [string] $Pictures,
    [string] $Out
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Pictures) { $Pictures = Join-Path $here '..\docs\screens' }
if (-not $Out)      { $Out      = Join-Path $here '..\docs\screens.mp4' }
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Media.Editing.MediaComposition, Windows.Media.Editing, ContentType = WindowsRuntime]
$null = [Windows.Media.Editing.MediaClip, Windows.Media.Editing, ContentType = WindowsRuntime]
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Storage.StorageFolder, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Media.MediaProperties.MediaEncodingProfile, Windows.Media.MediaProperties, ContentType = WindowsRuntime]
$null = [Windows.Media.Transcoding.TranscodeFailureReason, Windows.Media.Transcoding, ContentType = WindowsRuntime]

# The Windows Runtime answers later; these wait for the answer.
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 }
function Wait-Op($Op, [Type] $Result) {
    $m = $asTask | Where-Object { $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
    $t = $m.MakeGenericMethod($Result).Invoke($null, @($Op))
    $t.Wait(-1) | Out-Null
    return $t.Result
}
function Wait-OpProgress($Op, [Type] $Result, [Type] $Progress) {
    $m = $asTask | Where-Object { $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperationWithProgress`2' } | Select-Object -First 1
    $t = $m.MakeGenericMethod($Result, $Progress).Invoke($null, @($Op))
    $t.Wait(-1) | Out-Null
    return $t.Result
}

$dir   = (Resolve-Path -LiteralPath $Pictures).Path
$files = Get-ChildItem -LiteralPath $dir -Filter *.png | Sort-Object Name
if (-not $files) { throw "no pictures in $dir" }

$comp = New-Object Windows.Media.Editing.MediaComposition
$span = [TimeSpan]::FromSeconds($Seconds)
$addClip = [System.Collections.Generic.ICollection[Windows.Media.Editing.MediaClip]].GetMethod('Add')
foreach ($f in $files) {
    $sf   = Wait-Op ([Windows.Storage.StorageFile]::GetFileFromPathAsync($f.FullName)) ([Windows.Storage.StorageFile])
    $clip = Wait-Op ([Windows.Media.Editing.MediaClip]::CreateFromImageFileAsync($sf, $span)) ([Windows.Media.Editing.MediaClip])
    # PowerShell sees the list of clips as a bare COM object with no methods,
    # so Add is called through the interface the list implements.
    $addClip.Invoke($comp.Clips, @($clip))
}

$outPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
$folder  = Wait-Op ([Windows.Storage.StorageFolder]::GetFolderFromPathAsync((Split-Path $outPath))) ([Windows.Storage.StorageFolder])
$target  = Wait-Op ($folder.CreateFileAsync((Split-Path $outPath -Leaf), [Windows.Storage.CreationCollisionOption]::ReplaceExisting)) ([Windows.Storage.StorageFile])

$profile = [Windows.Media.MediaProperties.MediaEncodingProfile]::CreateMp4([Windows.Media.MediaProperties.VideoEncodingQuality]::Vga)
$profile.Audio = $null                              # there is no sound
$profile.Video.Bitrate = [uint32] $Bitrate          # still pictures: the profile's own rate is many times too much
$why = Wait-OpProgress ($comp.RenderToFileAsync($target, [Windows.Media.Editing.MediaTrimmingPreference]::Precise, $profile)) `
                       ([Windows.Media.Transcoding.TranscodeFailureReason]) ([double])
if ("$why" -ne 'None') { throw "the encoder gave up: $why" }

$len = (Get-Item -LiteralPath $outPath).Length
'{0} pictures, {1} s each, {2} s in all, {3} kB -> {4}' -f $files.Count, $Seconds, ($files.Count * $Seconds), [int] ($len / 1024), $outPath
