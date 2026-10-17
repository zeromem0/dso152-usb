<#
.SYNOPSIS
Takes what the DSO-152 TTL Sender puts on the calibration output and turns it
back into the bytes that were captured, checking it on the way.

.DESCRIPTION
Adapter RX to the calibration output, ground to ground. With -Port, start this
first, then press RUN on the instrument; it stops by itself once the line has
been quiet for -Idle seconds.

The format is recognised from the data: the hex dump (lines 'offset: hex text',
with '# ...' lines carrying block numbers and CRC-32), Intel HEX (lines
beginning ':'), or failing both, raw bytes. The bytes are written to -Out and
their CRC-32 is printed: it must be the one the instrument shows beside the
byte count.

Works in Windows PowerShell 5.1 and later; nothing to install. The same thing
in Python is ttl_receive.py.

.EXAMPLE
.\ttl_receive.ps1 -Port COM5 -Baud 9600

.EXAMPLE
.\ttl_receive.ps1 capture.log -Out scanner.bin

.EXAMPLE
.\ttl_receive.ps1 -ListPorts
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)] [string] $File,
    [string] $Port,
    [int]    $Baud = 9600,
    [double] $Idle = 2.0,
    [string] $Out  = 'received.bin',
    [switch] $Quiet,
    [switch] $ListPorts
)

$ErrorActionPreference = 'Stop'

if ($ListPorts) {
    [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    return
}

# --- CRC-32, the one of zip and zlib ---------------------------------------
$script:CrcTable = New-Object 'uint32[]' 256
for ($i = 0; $i -lt 256; $i++) {
    [uint32] $c = $i
    for ($k = 0; $k -lt 8; $k++) {
        if ($c -band 1) { $c = [uint32] (0xEDB88320L -bxor ($c -shr 1)) } else { $c = $c -shr 1 }
    }
    $script:CrcTable[$i] = $c
}
function Get-Crc32([byte[]] $Bytes) {
    [uint32] $c = [uint32]::MaxValue
    foreach ($b in $Bytes) {
        $c = [uint32] ($script:CrcTable[($c -bxor $b) -band 0xFF] -bxor ($c -shr 8))
    }
    return [uint32] ($c -bxor [uint32]::MaxValue)
}

function ConvertFrom-Hex([string] $Hex) {
    $h = $Hex -replace '\s', ''
    $n = [int] [math]::Floor($h.Length / 2)
    $b = New-Object 'byte[]' $n
    for ($i = 0; $i -lt $n; $i++) { $b[$i] = [Convert]::ToByte($h.Substring(2 * $i, 2), 16) }
    return , $b
}

# --- getting the characters ---------------------------------------------------
function Read-FromPort([string] $Name, [int] $Speed, [double] $Quiet) {
    $sp = New-Object System.IO.Ports.SerialPort $Name, $Speed, 'None', 8, 'One'
    $sp.ReadTimeout = 200
    # The instrument's own USB port sends only to a port the PC holds open,
    # which it is told by DTR; .NET leaves DTR off unless asked.
    $sp.DtrEnable = $true
    $ms = New-Object System.IO.MemoryStream
    $buf = New-Object 'byte[]' 4096
    $sp.Open()
    try {
        Write-Host "listening on $Name at $Speed 8N1; press RUN on the instrument (Ctrl+C gives up)"
        $last = $null
        while ($true) {
            $n = 0
            try { $n = $sp.Read($buf, 0, $buf.Length) } catch [System.TimeoutException] { $n = 0 }
            if ($n -gt 0) {
                $ms.Write($buf, 0, $n)
                $last = Get-Date
            }
            elseif ($null -ne $last -and ((Get-Date) - $last).TotalSeconds -gt $Quiet) { break }
        }
    }
    finally { $sp.Close() }
    return , $ms.ToArray()
}

if ($Port) { [byte[]] $raw = Read-FromPort $Port $Baud $Idle }
elseif ($File) { [byte[]] $raw = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $File).Path) }
else { throw 'Give a file, or -Port COMx. -ListPorts shows the ports there are.' }

$text  = [System.Text.Encoding]::GetEncoding(28591).GetString($raw)     # one character a byte
$lines = $text -split "`n" | ForEach-Object { $_.TrimEnd("`r") }

$reDump  = '^([0-9a-fA-F]{8}): '
$reBlock = '^# DSO152 block (\d+)/(\d+) offset (\d+) length (\d+)'
$reCrc   = '^# crc32 ([0-9A-Fa-f]{8})'
$reTotal = '^# total (\d+) bytes crc32 ([0-9A-Fa-f]{8})'

$mem      = @{}                                      # offset -> byte
$problems = New-Object System.Collections.Generic.List[string]
$notes    = New-Object System.Collections.Generic.List[string]
$total    = $null
$kind     = 'raw'

if ($lines | Where-Object { $_ -match $reDump } | Select-Object -First 1) {
    $kind = 'hex dump'
    $cur  = $null
    foreach ($ln in $lines) {
        if ($ln -match $reBlock) {
            $cur = @{ Blk = [int] $Matches[1]; Of = [int] $Matches[2]; Off = [int] $Matches[3]; Len = [int] $Matches[4]
                      Data = New-Object System.Collections.Generic.List[byte] }
        }
        elseif ($ln -match $reDump) {
            $off = [Convert]::ToInt32($Matches[1], 16)
            # the hex is in columns 10 to 49; what follows is the same bytes as text
            $hex = if ($ln.Length -ge 50) { $ln.Substring(10, 40) } else { $ln.Substring(10) }
            $b = ConvertFrom-Hex $hex
            for ($i = 0; $i -lt $b.Length; $i++) { $mem[$off + $i] = $b[$i] }
            if ($null -ne $cur) { $cur.Data.AddRange([byte[]] $b) }
        }
        elseif ($ln -match $reCrc -and $null -ne $cur) {
            $want = [Convert]::ToUInt32($Matches[1], 16)
            $got  = Get-Crc32 $cur.Data.ToArray()
            $ok   = ($want -eq $got) -and ($cur.Data.Count -eq $cur.Len)
            $verdict = if ($ok) { 'ok' } else { 'BAD (sent {0:X8}, {1} bytes)' -f $want, $cur.Len }
            $notes.Add(('block {0}/{1} at {2}: {3} bytes, crc32 {4:X8} {5}' -f $cur.Blk, $cur.Of, $cur.Off, $cur.Data.Count, $got, $verdict))
            if (-not $ok) { $problems.Add("block $($cur.Blk) is damaged: send it again") }
            $cur = $null
        }
        elseif ($ln -match $reTotal) {
            $total = @{ Len = [int] $Matches[1]; Crc = [Convert]::ToUInt32($Matches[2], 16) }
        }
    }
    if ($null -ne $cur) { $problems.Add("block $($cur.Blk) has no crc line: it was cut short") }
}
elseif ($lines | Where-Object { $_.StartsWith(':') } | Select-Object -First 1) {
    $kind  = 'Intel HEX'
    $ended = $false
    $n = 0
    foreach ($ln in $lines) {
        $n++
        if (-not $ln.StartsWith(':')) { continue }
        $body = $ln.Substring(1).Trim()
        if ($body -notmatch '^([0-9A-Fa-f]{2})+$') { $problems.Add("line ${n}: not hex"); continue }
        $rec = ConvertFrom-Hex $body
        $sum = 0
        foreach ($v in $rec) { $sum += $v }
        if ($rec.Length -lt 5 -or $rec.Length -ne $rec[0] + 5 -or ($sum -band 0xFF) -ne 0) {
            $problems.Add("line ${n}: bad length or checksum"); continue
        }
        $addr = ($rec[1] -shl 8) -bor $rec[2]
        if ($rec[3] -eq 0) { for ($i = 0; $i -lt $rec[0]; $i++) { $mem[$addr + $i] = $rec[4 + $i] } }
        elseif ($rec[3] -eq 1) { $ended = $true }
    }
    if (-not $ended) { $problems.Add('no end record: the export was cut short') }
}

if ($kind -eq 'raw') { [byte[]] $data = $raw }
elseif ($mem.Count -eq 0) { [byte[]] $data = @() }
else {
    # Measure-Object gives doubles, and a double is not found as a key of ints
    [int] $lo = ($mem.Keys | Measure-Object -Minimum).Minimum
    [int] $hi = ($mem.Keys | Measure-Object -Maximum).Maximum
    [byte[]] $data = New-Object 'byte[]' ($hi - $lo + 1)
    $missing = 0; $first = -1
    for ($a = $lo; $a -le $hi; $a++) {
        if ($mem.ContainsKey($a)) { $data[$a - $lo] = $mem[$a] }
        else { $missing++; if ($first -lt 0) { $first = $a } }
    }
    if ($missing) { $problems.Add("$missing bytes missing between offsets $lo and $hi, first at $first") }
    if ($lo -ne 0) { $problems.Add("starts at offset $lo, not 0: this is part of the buffer only") }
}

$crc = Get-Crc32 $data
if ($null -ne $total -and ($total.Len -ne $data.Length -or $total.Crc -ne $crc)) {
    $problems.Add(('the instrument sent {0} bytes with crc32 {1:X8}' -f $total.Len, $total.Crc))
}

"format: $kind   received: $($raw.Length) characters"
foreach ($t in $notes) { "  $t" }
'{0} bytes   CRC {1:X8}   <- compare with the instrument''s screen' -f $data.Length, $crc
if ($null -ne $total -and $problems.Count -eq 0) { 'the total line of the export agrees' }

if (-not $Quiet) {
    for ($i = 0; $i -lt $data.Length; $i += 16) {
        $n   = [math]::Min(16, $data.Length - $i)
        $row = $data[$i..($i + $n - 1)]
        $hex = ($row | ForEach-Object { $_.ToString('x2') }) -join ' '
        $txt = -join ($row | ForEach-Object { if ($_ -ge 32 -and $_ -lt 127) { [char] $_ } else { '.' } })
        '{0:x8}: {1,-47}  {2}' -f $i, $hex, $txt
    }
}

$outPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
[System.IO.File]::WriteAllBytes($outPath, $data)
"written to $outPath"
foreach ($p in $problems) { "PROBLEM: $p" }
if ($problems.Count) { exit 1 }
