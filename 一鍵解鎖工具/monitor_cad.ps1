# CAD File Extraction Monitor
# 這個腳本監控 TEMP 目錄，當 EXE 創建解密的 CAD 檔案時自動複製

$watchPath = $env:TEMP
$targetExtensions = @("*.dwg", "*.dxf", "*.dwt")
$outputDir = "D:\app\APP_TEST\cadlocker_patcher\captured"

# 創建輸出目錄
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

Write-Host "=========================================="
Write-Host "  CAD 檔案監控器"
Write-Host "=========================================="
Write-Host ""
Write-Host "監控目錄: $watchPath"
Write-Host "目標副檔名: $($targetExtensions -join ', ')"
Write-Host "捕獲輸出: $outputDir"
Write-Host ""
Write-Host "請在另一個視窗執行受保護的 EXE..."
Write-Host "按 Ctrl+C 停止監控"
Write-Host ""

# 創建 FileSystemWatcher
$watcher = New-Object System.IO.FileSystemWatcher
$watcher.Path = $watchPath
$watcher.IncludeSubdirectories = $true
$watcher.EnableRaisingEvents = $true

# 定義事件處理
$action = {
    $path = $Event.SourceEventArgs.FullPath
    $changeType = $Event.SourceEventArgs.ChangeType
    $timeStamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    
    $ext = [System.IO.Path]::GetExtension($path).ToLower()
    if ($ext -eq ".dwg" -or $ext -eq ".dxf" -or $ext -eq ".dwt") {
        Write-Host "[$timeStamp] $changeType : $path" -ForegroundColor Green
        
        # 等待檔案寫入完成
        Start-Sleep -Milliseconds 500
        
        # 複製檔案
        if (Test-Path $path) {
            $destPath = Join-Path "D:\app\APP_TEST\cadlocker_patcher\captured" (Split-Path $path -Leaf)
            Copy-Item $path $destPath -Force
            Write-Host "  -> 已捕獲到: $destPath" -ForegroundColor Yellow
        }
    }
}

# 註冊事件
Register-ObjectEvent $watcher "Created" -Action $action | Out-Null
Register-ObjectEvent $watcher "Changed" -Action $action | Out-Null

# 保持運行
try {
    while ($true) { Start-Sleep -Seconds 1 }
} finally {
    $watcher.EnableRaisingEvents = $false
    Get-EventSubscriber | Unregister-Event
}
