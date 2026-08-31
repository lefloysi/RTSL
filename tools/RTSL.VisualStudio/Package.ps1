param([ValidateSet('Debug','Release')] [string]$Configuration = 'Debug')
$root = Split-Path -Parent $PSScriptRoot
$server = Join-Path $root "rtsl-lsp\x64\$Configuration\rtsl-lsp.exe"
$assembly = Join-Path $PSScriptRoot "bin\$Configuration\net472\RTSL.VisualStudio.dll"
if (!(Test-Path -LiteralPath $server)) { throw "Build rtsl-lsp first: $server is missing." }
if (!(Test-Path -LiteralPath $assembly)) { throw "Build RTSL.VisualStudio first: $assembly is missing." }
$stage = Join-Path $PSScriptRoot "obj\vsix-staging"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'source.extension.vsixmanifest') -Destination (Join-Path $stage 'extension.vsixmanifest')
Copy-Item -LiteralPath $assembly -Destination $stage
Copy-Item -LiteralPath $server -Destination $stage
$output = Join-Path $PSScriptRoot "bin\$Configuration\RTSL.VisualStudio.vsix"
if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
@'
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="dll" ContentType="application/octet-stream" />
  <Default Extension="exe" ContentType="application/octet-stream" />
  <Default Extension="vsixmanifest" ContentType="text/xml" />
</Types>
'@ | Set-Content -LiteralPath (Join-Path $stage '[Content_Types].xml') -Encoding UTF8
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $output)
Write-Output $output
