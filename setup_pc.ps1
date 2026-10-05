$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not (Test-Path '.venv\Scripts\python.exe')) { py -3.13 -m venv .venv; if ($LASTEXITCODE) { throw 'Python 3.13 required' } }
& '.venv\Scripts\python.exe' -m pip install -r requirements-dev.txt
if ($LASTEXITCODE) { throw 'pip install failed' }
& '.venv\Scripts\python.exe' -m grpc_tools.protoc -I protocol --python_out=pc protocol/active_msgs.proto
if ($LASTEXITCODE) { throw 'protoc failed' }
