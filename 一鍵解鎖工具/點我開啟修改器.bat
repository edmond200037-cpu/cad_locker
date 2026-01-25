
@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

echo ==========================================
echo    CAD Locker 修改器啟動中...
echo ==========================================
echo.

:: 檢查 index.html 是否存在
if not exist "index.html" (
    echo [錯誤] 找不到 index.html 檔案，請確保此工具與修改器網頁在同一個資料夾。
    pause
    exit /b
)

echo 正在啟動專業圖形化修改介面...
start "" "index.html"

echo.
echo ==========================================
echo 已開啟瀏覽器介面。
echo 請直接將 EXE 檔案拖入網頁中進行修改。
echo ==========================================
timeout /t 5 >nul
exit
