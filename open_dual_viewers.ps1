$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
if (-not (Test-Path -LiteralPath $edge)) {
    $edge = 'C:\Program Files\Microsoft\Edge\Application\msedge.exe'
}
if (-not (Test-Path -LiteralPath $edge)) {
    throw 'Microsoft Edge is not installed.'
}
Start-Process -FilePath $edge -ArgumentList '--new-window', 'http://127.0.0.1:8081/'
Start-Sleep -Seconds 1
Start-Process -FilePath $edge -ArgumentList '--new-window', 'http://127.0.0.1:8082/'
