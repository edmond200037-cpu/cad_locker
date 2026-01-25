
Write-Host "=========================================="
Write-Host "  進階監控工具 - 進程與命令列追蹤"
Write-Host "=========================================="
Write-Host ""

$targetExe = "cad测试_t3_secure.exe"

# 監控子進程創建
Write-Host "[1] 監控進程創建事件..."
Write-Host ""

$query = "SELECT * FROM __InstanceCreationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'"

try {
    $watcher = New-Object System.Management.ManagementEventWatcher($query)
    $watcher.Options.Timeout = [System.Management.ManagementTimeout]::InfiniteTimeout
    
    Write-Host "正在監控... 請執行 $targetExe"
    Write-Host "觀察輸出中的命令列參數 (會顯示解密檔案路徑)"
    Write-Host ""
    Write-Host "按 Ctrl+C 停止"
    Write-Host "----------------------------------------"
    
    while ($true) {
        $event = $watcher.WaitForNextEvent()
        $process = $event.TargetInstance
        
        $name = $process.Name
        $cmdLine = $process.CommandLine
        $parentId = $process.ParentProcessId
        
        # 過濾相關進程
        if ($name -match "acad|dwg|autocad|cad" -or $cmdLine -match "\.dwg|\.dxf") {
            Write-Host ""
            Write-Host ">>> 發現相關進程!" -ForegroundColor Red
            Write-Host "    進程名: $name"
            Write-Host "    命令列: $cmdLine" -ForegroundColor Yellow
            Write-Host "    父進程ID: $parentId"
            
            # 嘗試從命令列提取檔案路徑
            if ($cmdLine -match '([A-Za-z]:\\[^"]+\.(dwg|dxf))') {
                $filePath = $matches[1]
                Write-Host ""
                Write-Host "*** 找到 CAD 檔案路徑: $filePath ***" -ForegroundColor Green
                
                # 嘗試複製
                if (Test-Path $filePath) {
                    $destPath = "D:\app\APP_TEST\cadlocker_patcher\captured\extracted_$(Get-Date -Format 'yyyyMMdd_HHmmss').dwg"
                    Copy-Item $filePath $destPath -Force
                    Write-Host "*** 已複製到: $destPath ***" -ForegroundColor Green
                }
            }
        }
    }
} catch {
    Write-Host "錯誤: $_"
} finally {
    if ($watcher) { $watcher.Dispose() }
}
