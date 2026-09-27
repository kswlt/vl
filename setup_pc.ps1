$ErrorActionPreference = 'Stop'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $dir
try {
    if (-not (Test-Path '.venv\Scripts\python.exe')) {
        py -3.13 -m venv .venv
        if ($LASTEXITCODE) { throw 'Python 3.13 venv creation failed' }
    }
    & '.venv\Scripts\python.exe' -m pip install -r requirements-dev.txt
    if ($LASTEXITCODE) { throw 'Dependency installation failed' }
    & '.venv\Scripts\python.exe' -m grpc_tools.protoc -I protocol --python_out=pc protocol/active_msgs.proto
    if ($LASTEXITCODE) { throw 'Protobuf generation failed' }
} finally { Pop-Location }
