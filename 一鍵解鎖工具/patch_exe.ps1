
param (
    [string]$FilePath = "cad测试_t3_secure.exe",
    [int]$NewLimit = 9999
)

$outPath = $FilePath.Replace(".exe", "_patched.exe")
Write-Host "正在讀取: $FilePath ..."

if (-not (Test-Path $FilePath)) {
    Write-Host "錯誤: 找不到檔案 $FilePath" -ForegroundColor Red
    return
}

$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $FilePath))

# 尋找結尾的 CADLOCK 標記
$sig = "CADLOCK`0"
$sigBytes = [System.Text.Encoding]::ASCII.GetBytes($sig)
$foundIndex = -1

for ($i = $bytes.Length - $sigBytes.Length; $i -ge ($bytes.Length - 100); $i--) {
    $match = $true
    for ($j = 0; $j -lt $sigBytes.Length; $j++) {
        if ($bytes[$i + $j] -ne $sigBytes[$j]) { $match = $false; break }
    }
    if ($match) { $foundIndex = $i; break }
}

if ($foundIndex -ne -1) {
    # 限制在標記前 4 個位元組
    $limitPos = $foundIndex - 4
    $currentLimit = $bytes[$limitPos] + ($bytes[$limitPos+1] -shl 8)
    Write-Host "偵測到 CADLOCK 標記，目前限制為: $currentLimit 次。"
    
    $bytes[$limitPos] = $NewLimit -band 0xFF
    $bytes[$limitPos+1] = ($NewLimit -shr 8) -band 0xFF
    $bytes[$limitPos+2] = ($NewLimit -shr 16) -band 0xFF
    $bytes[$limitPos+3] = ($NewLimit -shr 24) -band 0xFF
    
    [System.IO.File]::WriteAllBytes($outPath, $bytes)
    Write-Host "修改成功！已更新限制為 $NewLimit 次。" -ForegroundColor Green
    Write-Host "存檔至: $outPath" -ForegroundColor Yellow
    
    # 自動解除鎖定
    Unblock-File -Path $outPath
    Write-Host "已自動解除 Windows 鎖定。" -ForegroundColor Green
} else {
    Write-Host "找不到結尾的 CADLOCK 標記，此檔案格式可能不同。" -ForegroundColor Red
}
