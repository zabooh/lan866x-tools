# T1S 100BaseT Bridge - Harmony 3 Repository Setup (PowerShell)
# This script clones all required Harmony 3 repositories with their specific versions
# Based on harmony-manifest-success.yml

param(
    [switch]$Clean,
    [switch]$Update,
    [switch]$Status,
    [switch]$Help
)

# Configuration
$HarmonyRoot = "C:\Users\M91221\.mcc\HarmonyContent"
$ProjectName = "T1S 100BaseT Bridge"

# Repository definitions from harmony-manifest-success.yml
$Repositories = @(
    @{ Name = "core"; Version = "v3.13.4"; Url = "https://github.com/Microchip-MPLAB-Harmony/core.git" },
    @{ Name = "csp"; Version = "v3.18.5"; Url = "https://github.com/Microchip-MPLAB-Harmony/csp.git" },
    @{ Name = "dev_packs"; Version = "v3.18.1"; Url = "https://github.com/Microchip-MPLAB-Harmony/dev_packs.git" },
    @{ Name = "net"; Version = "v3.11.1"; Url = "https://github.com/Microchip-MPLAB-Harmony/net.git" },
    @{ Name = "net_10base_t1s"; Version = "v1.3.2"; Url = "https://github.com/Microchip-MPLAB-Harmony/net_10base_t1s.git" },
    @{ Name = "crypto"; Version = "v3.8.1"; Url = "https://github.com/Microchip-MPLAB-Harmony/crypto.git" },
    @{ Name = "wolfssl"; Version = "v5.4.0"; Url = "https://github.com/Microchip-MPLAB-Harmony/wolfssl.git" }
)

function Show-Help {
    Write-Host "T1S 100BaseT Bridge - Harmony 3 Repository Setup" -ForegroundColor Cyan
    Write-Host "=================================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  .\setup_harmony_repos.ps1           # Initial setup (clone all repos)"
    Write-Host "  .\setup_harmony_repos.ps1 -Update   # Update existing repos"
    Write-Host "  .\setup_harmony_repos.ps1 -Status   # Check repo status"
    Write-Host "  .\setup_harmony_repos.ps1 -Clean    # Clean setup (delete and re-clone)"
    Write-Host "  .\setup_harmony_repos.ps1 -Help     # Show this help"
    Write-Host ""
    Write-Host "Required Repositories:" -ForegroundColor Yellow
    foreach ($repo in $Repositories) {
        Write-Host "  $($repo.Name) ($($repo.Version))" -ForegroundColor Gray
    }
    Write-Host ""
    Write-Host "Target Directory: $HarmonyRoot" -ForegroundColor Gray
}

function Test-GitInstalled {
    try {
        $null = Get-Command git -ErrorAction Stop
        return $true
    }
    catch {
        Write-Error "Git is not installed or not in PATH. Please install Git from https://git-scm.com/"
        return $false
    }
}

function Initialize-HarmonyDirectory {
    if (-not (Test-Path $HarmonyRoot)) {
        Write-Host "Creating Harmony 3 root directory: $HarmonyRoot" -ForegroundColor Green
        New-Item -ItemType Directory -Path $HarmonyRoot -Force | Out-Null
    }
    
    Set-Location $HarmonyRoot
    Write-Host "Working in: $(Get-Location)" -ForegroundColor Cyan
}

function Clone-Repository {
    param($Repository, $Index, $Total)
    
    Write-Host ""
    Write-Host "[$Index/$Total] Processing $($Repository.Name) $($Repository.Version)..." -ForegroundColor Cyan
    
    if (Test-Path $Repository.Name) {
        Write-Host "Repository $($Repository.Name) already exists, skipping clone..." -ForegroundColor Yellow
    }
    else {
        Write-Host "Cloning $($Repository.Name)..." -ForegroundColor Green
        & git clone $Repository.Url
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to clone $($Repository.Name)"
        }
    }
    
    Set-Location $Repository.Name
    Write-Host "Checking out $($Repository.Version)..." -ForegroundColor Green
    & git checkout $Repository.Version
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to checkout $($Repository.Name) $($Repository.Version)"
    }
    Set-Location ..
}

function Update-Repository {
    param($Repository, $Index, $Total)
    
    Write-Host ""
    Write-Host "[$Index/$Total] Updating $($Repository.Name)..." -ForegroundColor Cyan
    
    if (-not (Test-Path $Repository.Name)) {
        Write-Host "Repository $($Repository.Name) not found, cloning..." -ForegroundColor Yellow
        & git clone $Repository.Url
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to clone $($Repository.Name)"
        }
    }
    
    Set-Location $Repository.Name
    & git fetch --tags
    & git checkout $Repository.Version
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Could not checkout $($Repository.Version) for $($Repository.Name)"
    }
    Set-Location ..
}

function Show-RepositoryStatus {
    if (-not (Test-Path $HarmonyRoot)) {
        Write-Error "Harmony 3 directory does not exist: $HarmonyRoot"
        return
    }
    
    Set-Location $HarmonyRoot
    Write-Host ""
    Write-Host "Repository Status:" -ForegroundColor Cyan
    Write-Host ""
    
    foreach ($repo in $Repositories) {
        if (Test-Path $repo.Name) {
            Set-Location $repo.Name
            try {
                $currentTag = & git describe --tags --exact-match HEAD 2>$null
                if ($currentTag -eq $repo.Version) {
                    Write-Host "✓ $($repo.Name): $currentTag (correct)" -ForegroundColor Green
                }
                else {
                    Write-Host "✗ $($repo.Name): $currentTag (expected $($repo.Version))" -ForegroundColor Red
                }
            }
            catch {
                Write-Host "✗ $($repo.Name): Unable to determine version" -ForegroundColor Red
            }
            Set-Location ..
        }
        else {
            Write-Host "✗ $($repo.Name): NOT FOUND" -ForegroundColor Red
        }
    }
    Write-Host ""
}

# Main execution
try {
    if ($Help) {
        Show-Help
        exit 0
    }
    
    if (-not (Test-GitInstalled)) {
        exit 1
    }
    
    Write-Host "T1S 100BaseT Bridge - Harmony 3 Setup" -ForegroundColor Cyan
    Write-Host "=====================================" -ForegroundColor Cyan
    Write-Host ""
    
    if ($Status) {
        Show-RepositoryStatus
        exit 0
    }
    
    if ($Clean) {
        Write-Host "Clean Setup - Removing existing Harmony 3 directory..." -ForegroundColor Yellow
        if (Test-Path $HarmonyRoot) {
            Remove-Item -Recurse -Force $HarmonyRoot
        }
    }
    
    Initialize-HarmonyDirectory
    
    if ($Update) {
        Write-Host "Updating Existing Repositories" -ForegroundColor Cyan
        Write-Host "==============================" -ForegroundColor Cyan
        
        for ($i = 0; $i -lt $Repositories.Count; $i++) {
            Update-Repository $Repositories[$i] ($i + 1) $Repositories.Count
        }
        
        Write-Host ""
        Write-Host "✓ Update completed!" -ForegroundColor Green
    }
    else {
        Write-Host "Initial Setup - Cloning All Repositories" -ForegroundColor Cyan
        Write-Host "========================================" -ForegroundColor Cyan
        
        for ($i = 0; $i -lt $Repositories.Count; $i++) {
            Clone-Repository $Repositories[$i] ($i + 1) $Repositories.Count
        }
        
        Write-Host ""
        Write-Host "✓ Initial setup completed successfully!" -ForegroundColor Green
    }
    
    Write-Host ""
    Write-Host "All Harmony 3 repositories are ready at:" -ForegroundColor Green
    Write-Host "$(Resolve-Path $HarmonyRoot)" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Repository Versions:" -ForegroundColor Green
    foreach ($repo in $Repositories) {
        Write-Host "- $($repo.Name): $($repo.Version)" -ForegroundColor Gray
    }
    Write-Host ""
    Write-Host "You can now build the T1S 100BaseT Bridge project." -ForegroundColor Green
}
catch {
    Write-Error "Setup failed: $($_.Exception.Message)"
    Write-Host ""
    Write-Host "Please check your internet connection and Git installation." -ForegroundColor Yellow
    Write-Host "Make sure you have access to the Microchip-MPLAB-Harmony GitHub repositories." -ForegroundColor Yellow
    exit 1
}