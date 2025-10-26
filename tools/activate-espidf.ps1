param(
    [string]$IdfPath = "C:/Espressif/frameworks/esp-idf-v5.5.1/export.ps1",
    [switch]$Print
)

if (-not (Test-Path $IdfPath)) {
    Write-Error "export.ps1 nicht gefunden: $IdfPath"; exit 1
}

# Dot-Source, damit PATH/Functions in der aktuellen Session landen
. $IdfPath

if ($Print) {
    try {
        idf.py --version
    } catch {
        Write-Warning "idf.py ist nicht im PATH verfügbar. Starte die Shell ggf. neu."
    }
}
