# T1S 100BaseT Bridge - Harmony 3 Version Checker and Corrector (PowerShell)
# This script checks and corrects repository versions to match project requirements

param(
    [switch]$Force,
    [switch]$DryRun,
    [switch]$Verbose,
    [switch]$Help
)

# Configuration
$HarmonyRoot = "C:\Users\M91221\.mcc\HarmonyContent"
$ProjectName = "T1S 100BaseT Bridge"

# Repository definitions from harmony-manifest-success.yml
$RequiredRepositories = @(
    @{ Name = "core"; Version = "v3.13.4" },
    @{ Name = "csp"; Version = "v3.18.5" },
    @{ Name = "dev_packs"; Version = "v3.18.1" },
    @{ Name = "net"; Version = "v3.11.1" },
    @{ Name = "net_10base_t1s"; Version = "v1.3.2" },
    @{ Name = "crypto"; Version = "v3.8.1" },
    @{ Name = "wolfssl"; Version = "v5.4.0" }
)

function Show-Help {
    Write-Host "$ProjectName - Harmony 3 Version Checker" -ForegroundColor Cyan
    Write-Host "======================================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "This script checks and corrects Harmony 3 repository versions."
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  .\check_harmony_versions.ps1           # Check and correct versions"
    Write-Host "  .\check_harmony_versions.ps1 -DryRun   # Check only (no changes)"
    Write-Host "  .\check_harmony_versions.ps1 -Force    # Force checkout even if dirty"
    Write-Host "  .\check_harmony_versions.ps1 -Verbose  # Show detailed output"
    Write-Host "  .\check_harmony_versions.ps1 -Help     # Show this help"
    Write-Host ""
    Write-Host "Target Directory: $HarmonyRoot" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Required Versions:" -ForegroundColor Yellow
    foreach ($repo in $RequiredRepositories) {
        Write-Host "  $($repo.Name): $($repo.Version)" -ForegroundColor Gray
    }
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

function Get-CurrentVersion {
    param($RepositoryPath)
    
    try {
        # Try to get exact tag first
        $tag = & git describe --tags --exact-match HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $tag) {
            return $tag.Trim()
        }
        
        # Get current branch name
        $branch = & git rev-parse --abbrev-ref HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $branch -and $branch -ne "HEAD") {
            return "branch: $($branch.Trim())"
        }
        
        # Get commit hash as fallback
        $commit = & git rev-parse --short HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $commit) {
            return "commit: $($commit.Trim())"
        }
        
        return "unknown"
    }
    catch {
        return "error"
    }
}

function Test-WorkingDirectoryClean {
    try {
        $status = & git status --porcelain 2>$null
        return [string]::IsNullOrWhiteSpace($status)
    }
    catch {
        return $false
    }
}

function Set-RepositoryVersion {
    param($Repository, $DryRun = $false, $Force = $false)
    
    if (-not (Test-Path $Repository.Name)) {
        return @{
            Success = $false
            Status = "not-found"
            Message = "Repository directory not found"
            CurrentVersion = "N/A"
        }
    }
    
    Push-Location $Repository.Name
    try {
        $currentVersion = Get-CurrentVersion
        
        if ($currentVersion -eq $Repository.Version) {
            return @{
                Success = $true
                Status = "correct"
                Message = "Already at correct version"
                CurrentVersion = $currentVersion
            }
        }
        
        if ($DryRun) {
            return @{
                Success = $false
                Status = "needs-update"
                Message = "Would update to $($Repository.Version)"
                CurrentVersion = $currentVersion
            }
        }
        
        # Check if working directory is clean
        if (-not $Force -and -not (Test-WorkingDirectoryClean)) {
            return @{
                Success = $false
                Status = "dirty"
                Message = "Working directory has uncommitted changes (use -Force to override)"
                CurrentVersion = $currentVersion
            }
        }
        
        # Fetch latest tags
        if ($Verbose) { Write-Host "    Fetching latest tags..." -ForegroundColor Gray }
        & git fetch --tags 2>$null | Out-Null
        
        # Stash changes if forcing and directory is dirty
        if ($Force -and -not (Test-WorkingDirectoryClean)) {
            if ($Verbose) { Write-Host "    Stashing changes..." -ForegroundColor Gray }
            & git stash 2>$null | Out-Null
        }
        
        # Checkout the required version
        & git checkout $Repository.Version 2>$null
        if ($LASTEXITCODE -eq 0) {
            $newVersion = Get-CurrentVersion
            return @{
                Success = $true
                Status = "corrected"
                Message = "Successfully updated"
                CurrentVersion = $newVersion
            }
        } else {
            return @{
                Success = $false
                Status = "checkout-failed"
                Message = "Failed to checkout version $($Repository.Version)"
                CurrentVersion = $currentVersion
            }
        }
    }
    finally {
        Pop-Location
    }
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
    
    Write-Host "$ProjectName - Version Checker" -ForegroundColor Cyan
    Write-Host "==========================================" -ForegroundColor Cyan
    Write-Host ""
    
    if (-not (Test-Path $HarmonyRoot)) {
        Write-Error "Harmony 3 directory does not exist: $HarmonyRoot"
        Write-Host "Please run setup_harmony_repos.ps1 first to clone the repositories." -ForegroundColor Yellow
        exit 1
    }
    
    Push-Location $HarmonyRoot
    Write-Host "Working in: $(Get-Location)" -ForegroundColor Cyan
    Write-Host ""
    
    if ($DryRun) {
        Write-Host "DRY RUN MODE - No changes will be made" -ForegroundColor Yellow
        Write-Host ""
    }
    
    $results = @()
    $totalRepos = $RequiredRepositories.Count
    
    Write-Host "Checking $totalRepos repositories..." -ForegroundColor Green
    Write-Host ""
    
    for ($i = 0; $i -lt $RequiredRepositories.Count; $i++) {
        $repo = $RequiredRepositories[$i]
        $index = $i + 1
        
        Write-Host "[$index/$totalRepos] Checking $($repo.Name)..." -ForegroundColor Cyan
        
        $result = Set-RepositoryVersion -Repository $repo -DryRun:$DryRun -Force:$Force
        $result.Repository = $repo.Name
        $result.RequiredVersion = $repo.Version
        $results += $result
        
        switch ($result.Status) {
            "correct" {
                Write-Host "  ✓ $($repo.Name): $($result.CurrentVersion) (correct)" -ForegroundColor Green
            }
            "corrected" {
                Write-Host "  ✓ $($repo.Name): Updated to $($result.CurrentVersion)" -ForegroundColor Green
            }
            "needs-update" {
                Write-Host "  → $($repo.Name): $($result.CurrentVersion) → would update to $($repo.Version)" -ForegroundColor Yellow
            }
            "not-found" {
                Write-Host "  ✗ $($repo.Name): NOT FOUND" -ForegroundColor Red
                Write-Host "    Run setup_harmony_repos.ps1 first to clone repositories" -ForegroundColor Gray
            }
            "dirty" {
                Write-Host "  ⚠ $($repo.Name): $($result.CurrentVersion) - $($result.Message)" -ForegroundColor Yellow
            }
            "checkout-failed" {
                Write-Host "  ✗ $($repo.Name): $($result.Message)" -ForegroundColor Red
            }
            default {
                Write-Host "  ? $($repo.Name): $($result.Message)" -ForegroundColor Gray
            }
        }
        
        if ($Verbose -and $result.Message) {
            Write-Host "    $($result.Message)" -ForegroundColor Gray
        }
        Write-Host ""
    }
    
    # Summary
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Summary" -ForegroundColor Cyan  
    Write-Host "========================================" -ForegroundColor Cyan
    
    $correct = ($results | Where-Object { $_.Status -eq "correct" }).Count
    $corrected = ($results | Where-Object { $_.Status -eq "corrected" }).Count
    $needsUpdate = ($results | Where-Object { $_.Status -eq "needs-update" }).Count
    $errors = ($results | Where-Object { $_.Success -eq $false -and $_.Status -ne "needs-update" }).Count
    
    Write-Host "Total repositories: $totalRepos" -ForegroundColor Gray
    Write-Host "Already correct: $correct" -ForegroundColor Green
    
    if ($DryRun) {
        Write-Host "Need updates: $needsUpdate" -ForegroundColor Yellow
    } else {
        Write-Host "Corrected: $corrected" -ForegroundColor Green
    }
    
    Write-Host "Errors: $errors" -ForegroundColor Red
    Write-Host ""
    
    if ($errors -eq 0) {
        if ($DryRun -and $needsUpdate -gt 0) {
            Write-Host "✓ Check completed. Run without -DryRun to apply updates." -ForegroundColor Yellow
        } elseif ($corrected -eq 0 -and -not $DryRun) {
            Write-Host "✓ All repositories are already at the correct versions!" -ForegroundColor Green
        } else {
            Write-Host "✓ Version check and correction completed successfully!" -ForegroundColor Green
        }
        Write-Host ""
        Write-Host "Your Harmony 3 repositories are ready for the T1S 100BaseT Bridge project." -ForegroundColor Green
        exit 0
    } else {
        Write-Host "✗ Some repositories could not be processed correctly." -ForegroundColor Red
        Write-Host "Please check the error messages above." -ForegroundColor Yellow
        exit 1
    }
}
catch {
    Write-Error "Version check failed: $($_.Exception.Message)"
    exit 1
}
finally {
    Pop-Location
}