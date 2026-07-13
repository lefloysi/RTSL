param(
    [ValidateSet("targeted", "full")]
    [string]$Scope = "targeted",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $repoRoot "out/build"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$CommandLine
    )

    $Command = $CommandLine[0]
    $Arguments = @()
    if ($CommandLine.Count -gt 1) {
        $Arguments = $CommandLine[1..($CommandLine.Count - 1)]
    }
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

if ($Scope -eq "targeted") {
    Invoke-Checked @("cmake", "-S", $repoRoot, "-B", $buildDir, "-DCMAKE_BUILD_TYPE=$Config")
    Invoke-Checked @("cmake", "--build", $buildDir, "--config", $Config, "--target", "rtsl-tests", "--parallel")
    Invoke-Checked @("ctest", "--test-dir", $buildDir, "-C", $Config, "--output-on-failure", "-R", "rtsl-tests")
} else {
    Invoke-Checked @("cmake", "-S", $repoRoot, "-B", $buildDir, "-DCMAKE_BUILD_TYPE=$Config")
    Invoke-Checked @("cmake", "--build", $buildDir, "--config", $Config, "--parallel")
    Invoke-Checked @("ctest", "--test-dir", $buildDir, "-C", $Config, "--output-on-failure")
}
