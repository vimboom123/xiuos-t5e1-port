# 烧一个【物理镜像】到指定地址（bootrom 重启），然后在同一个口上抓日志
#   -Img   已经带 CRC、且长度是 4K 整数倍的镜像（bk_loader 会把写入长度向下截到 4K！）
#   -Watch 只监听不烧写
param(
    [string]$Port = 'COM4',
    [string]$Img  = 'D:\ds\XiZi-uart0-v2_crc4k.bin',
    [string]$Addr = '0x132000',
    [int]$FlashBaud = 460800,
    [int]$LogBaud = 115200,
    [int]$LogSeconds = 120,
    [string]$Tag = 'xizi',
    [switch]$Watch
)
$ErrorActionPreference = 'Continue'
$bk = 'D:\ds\bk_loader\bk_loader.exe'

if (-not $Watch) {
    $len = (Get-Item $Img).Length
    Write-Output "=== 镜像 $Img  $len 字节  34倍数=$($len % 34 -eq 0)  4K倍数=$($len % 4096 -eq 0) ==="
    if ($len % 4096) { Write-Output "长度不是 4K 整数倍，bk_loader 会丢尾巴，停止"; exit 1 }
    $ok = $false
    for ($i = 1; $i -le 12 -and -not $ok; $i++) {
        Write-Output "[$(Get-Date -f HH:mm:ss)] 烧写尝试 $i —— 请短接 CEN→GND 再松开"
        $r = & $bk download -p $Port -b $FlashBaud -i $Img -s $Addr -e 1 -r -g 600 -d 3 2>&1
        $r | Select-String 'Get bus s|length:|all pass|fail' | ForEach-Object { "  $_" }
        if ($r -match 'all pass') { $ok = $true }
    }
    if (-not $ok) { Write-Output "=== 烧写未成功，停止 ==="; exit 1 }
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $LogBaud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
$sb = New-Object System.Text.StringBuilder
$raw = New-Object System.Collections.Generic.List[byte]
$lines = 0
try {
    $sp.Open()
    Write-Output "[$(Get-Date -f HH:mm:ss)] 监听 $Port @ $LogBaud，$LogSeconds 秒"
    $t0 = Get-Date; $lastPrompt = -100; $lastPoke = 0
    $buf = New-Object byte[] 4096
    while (((Get-Date) - $t0).TotalSeconds -lt $LogSeconds) {
        $el = [int]((Get-Date) - $t0).TotalSeconds
        if ($raw.Count -eq 0 -and $el -ge 8 -and $el - $lastPrompt -ge 20) {
            Write-Output "[$(Get-Date -f HH:mm:ss)] 仍然 0 字节 —— 请短接一次 CEN→GND"
            $lastPrompt = $el
        }
        if ($el - $lastPoke -ge 10) { $sp.Write("`r`n"); $lastPoke = $el }
        try {
            $k = $sp.Read($buf, 0, $buf.Length)
            if ($k -gt 0) {
                for ($q = 0; $q -lt $k; $q++) { $raw.Add($buf[$q]) }
                if ($raw.Count -eq $k) { Write-Output "[$(Get-Date -f HH:mm:ss)] 首次收到字节" }
                $s = [Text.Encoding]::ASCII.GetString($buf, 0, $k)
                [void]$sb.Append($s)
                foreach ($ln in ($s -split "`n")) {
                    if ($ln.Trim() -and $lines -lt 400) { Write-Output "  | $($ln.TrimEnd())"; $lines++ }
                }
            }
        } catch [TimeoutException] { }
    }
} finally {
    if ($sp.IsOpen) { $sp.Close() }
    $sp.Dispose()
}
$txt = $sb.ToString()
$out = "D:\ds\boot-$Tag-$LogBaud.txt"
$txt | Set-Content $out -Encoding utf8
[IO.File]::WriteAllBytes("D:\ds\boot-$Tag-$LogBaud.bin", $raw.ToArray())
$zeros = ($raw | Where-Object { $_ -eq 0 }).Count
Write-Output "=== 共收到 $($raw.Count) 字节（其中 0x00/break $zeros 个），已存 $out ==="
foreach ($kw in 'bk7258 uart:', 'frc:', 'letter-shell', 'letter:', 'Fault', 'cpu1', 'ap0', 'wifid', '[XZ]') {
    if ($txt.Contains($kw)) { Write-Output "  命中: $kw" }
}
