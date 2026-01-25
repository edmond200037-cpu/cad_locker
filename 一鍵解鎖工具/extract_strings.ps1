
$file = "cad测试_t3_secure.exe"
$bytes = [System.IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $file))
$strings = [System.Text.Encoding]::ASCII.GetString($bytes)
$matches = [regex]::Matches($strings, '[\x20-\x7E]{4,}')
foreach ($m in $matches) {
    if ($m.Value -match "limit|count|times|expire|open|lock|protect") {
        Write-Host "$($m.Index): $($m.Value)"
    }
}
