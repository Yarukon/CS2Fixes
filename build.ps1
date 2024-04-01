Set-Location  $PSScriptRoot

Set-Location "./build"
python.exe ../configure.py --enable-optimize --symbol-files --sdks cs2
ambuild
