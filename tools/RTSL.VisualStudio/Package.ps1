param([ValidateSet('Debug', 'Release')] [string]$Configuration = 'Release')

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw "Visual Studio Installer was not found: $vswhere" }

$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VSSDK -property installationPath
if (!$installation) { throw 'A Visual Studio 2022 installation with the VSSDK is required.' }

$msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
if (!(Test-Path -LiteralPath $msbuild)) { throw "Visual Studio MSBuild was not found: $msbuild" }

$server = Join-Path $PSScriptRoot "..\rtsl-lsp\x64\$Configuration\rtsl-lsp.exe"
if (!(Test-Path -LiteralPath $server)) { throw "Build rtsl-lsp first: $server is missing." }

& $msbuild (Join-Path $PSScriptRoot 'RTSL.VisualStudio.csproj') '/t:Build;CreateVsixContainer' "/p:Configuration=$Configuration" '/p:DeployExtension=false'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$vsix = Join-Path $PSScriptRoot "bin\$Configuration\RTSL.VisualStudio.vsix"
if (!(Test-Path -LiteralPath $vsix)) { throw "VSSDK did not produce the VSIX: $vsix" }
Write-Output $vsix
