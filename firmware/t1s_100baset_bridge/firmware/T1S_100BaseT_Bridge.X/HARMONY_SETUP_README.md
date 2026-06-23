# Harmony 3 Repository Setup

Dieses Verzeichnis enthält mehrere Skripte zum automatischen Klonen und Verwalten der für das T1S 100BaseT Bridge Projekt benötigten Harmony 3 Repositories.

## Benötigte Repositories

Basierend auf der `harmony-manifest-success.yml` werden folgende Repositories mit spezifischen Versionen benötigt:

| Repository | Version | Beschreibung |
|------------|---------|--------------|
| core | v3.13.4 | Harmony 3 Core Framework |
| csp | v3.18.5 | Chip Support Package (ATSAME54P20A) |
| dev_packs | v3.18.1 | Device Packs |
| net | v3.11.1 | Networking Components (TCP/IP Stack) |
| net_10base_t1s | v1.3.2 | 10BASE-T1S Networking Support |
| crypto | v3.8.1 | Cryptographic Library |
| wolfssl | v5.4.0 | WolfSSL Security Library |

## ⚠️ Wichtiger Hinweis: Vollständige vs. Minimale Installation

Die `harmony-manifest-success.yml` zeigt nur die **im Projekt verwendeten** Module (7 Repositories). MCC erwartet jedoch normalerweise eine **vollständigere Harmony 3 Installation** für optimale Funktionalität.

### 🎯 Installationsoptionen:

| Option | Repositories | MCC-Kompatibilität | Verwendung |
|--------|-------------|-------------------|------------|
| **Minimal** | 7 (nur projekt-spezifisch) | Grundlegend | Nur T1S-Projekt |
| **Standard** | ~12 (empfohlen) | Vollständig | Typische MCC-Nutzung |
| **Komplett** | ~20 (alle verfügbar) | Maximal | Erweiterte Entwicklung |

## Verfügbare Skripte

## Verfügbare Skripte

### 🚀 **Master-Skript (empfohlener Einstieg)**

#### `harmony_master_setup.bat` (Interaktiver Guide)
Hauptskript mit geführtem Menü für alle Operationen.

```cmd
harmony_master_setup.bat
```

**Features:**
- Interaktives Menü-System
- Geführter Installations-Workflow
- Automatische Analyse und Empfehlungen
- Integration aller Sub-Skripte

### 📦 **Installations-Skripte**

#### 1. `setup_harmony_repos.bat` (Minimal - Nur Projekt-spezifisch)
Installiert nur die 7 für das T1S-Projekt benötigten Repositories.

```cmd
setup_harmony_repos.bat
```

#### 2. `setup_harmony_complete.bat` (Vollständig - Empfohlen)
Installiert alle Standard-Harmony 3 Repositories für vollständige MCC-Kompatibilität.

```cmd
setup_harmony_complete.bat
```

**Installationsmodi:**
- **Required** (`R`): Nur projekt-spezifische (7 Repos)
- **Standard** (`S`): +Typische MCC-Bibliotheken (~12 Repos) - **Empfohlen**
- **Full** (`F`): Alle verfügbaren (~20 Repos)

#### 3. `setup_harmony_repos_advanced.bat` (Legacy - Erweiterte Optionen)
Original erweiterte Version mit Menü-System.

### 🔍 **Analyse-Skripte**

#### 4. `analyze_harmony_repos.bat` (Repository-Analyse)
Analysiert die aktuelle Installation und gibt Empfehlungen.

```cmd
analyze_harmony_repos.bat
```

**Features:**
- Kategorisiert Repositories (Required/Standard/Optional)
- MCC-Kompatibilitäts-Check
- Empfehlungen für fehlende Repositories
- Unbekannte Repository-Erkennung

### ✅ **Versions-Management**

#### 5. `check_harmony_versions.bat` (Version Checker - Erweitert)
Vollständiger Batch-Versions-Checker mit Command-Line-Optionen.

```cmd
# Basis-Check und Korrektur
check_harmony_versions.bat

# Erweiterte Optionen
check_harmony_versions.bat -dryrun         # Nur prüfen, keine Änderungen
check_harmony_versions.bat -force          # Erzwingen auch bei uncommitted changes  
check_harmony_versions.bat -verbose        # Detaillierte Ausgaben
check_harmony_versions.bat -help           # Hilfe anzeigen
```

#### 6. `check_harmony_versions.ps1` (PowerShell-Alternative)
PowerShell-Version mit modernen Features.

```powershell
.\check_harmony_versions.ps1 -DryRun
.\check_harmony_versions.ps1 -Force  
.\check_harmony_versions.ps1 -Verbose
```

## Verwendung

## Verwendung

### 🚀 **Empfohlener Workflow (Erstmalige Einrichtung)**

```cmd
# 1. Master-Setup starten (empfohlener Weg)
harmony_master_setup.bat

# Oder direkt:
# 2. Standard-Installation (empfohlen für MCC)
setup_harmony_complete.bat
# Wählen Sie 'S' für Standard-Installation

# 3. Versionen prüfen
check_harmony_versions.bat
```

### 🔍 **Analyse bestehender Installation**

```cmd
# Aktuelle Installation analysieren
analyze_harmony_repos.bat

# Gibt detaillierte Empfehlungen basierend auf:
# - Vorhandene Repositories
# - MCC-Kompatibilität  
# - Projekt-Anforderungen
```

### ⚡ **Schnelle Optionen**

```cmd
# Nur T1S-Projekt-Dependencies (minimal)
setup_harmony_repos.bat

# Vollständige MCC-Installation (empfohlen)  
setup_harmony_complete.bat

# Version-Check ohne Änderungen
check_harmony_versions.bat -dryrun
```

### Erstmalige Einrichtung

1. **Repository-Analyse:**
   ```cmd
   # Prüfen was bereits vorhanden ist
   analyze_harmony_repos.bat
   ```

2. **Installationsmodus wählen:**
   - **Minimal:** Nur für T1S-Projekt → `setup_harmony_repos.bat`
   - **Standard:** Vollständige MCC-Unterstützung → `setup_harmony_complete.bat` + `S`
   - **Komplett:** Alle Bibliotheken → `setup_harmony_complete.bat` + `F`

3. **Versionen validieren:**
   ```cmd
   check_harmony_versions.bat
   ```

### Updates

Um bestehende Repositories zu aktualisieren:

```powershell
# PowerShell
.\setup_harmony_repos.ps1 -Update

# Oder Advanced Batch (Option 2 wählen)
setup_harmony_repos_advanced.bat
```

### Version-Überprüfung

Um zu prüfen und korrigieren, ob alle Repositories die korrekten Versionen haben:

```cmd
# Batch (empfohlen)
check_harmony_versions.bat

# Nur prüfen ohne Änderungen
check_harmony_versions.bat -dryrun

# PowerShell-Alternative
.\check_harmony_versions.ps1
```

### Workflow-Empfehlung

1. **Einmalig:** `setup_harmony_repos.bat` - Initial setup
2. **Regelmäßig:** `check_harmony_versions.bat` - Version-Check vor Build  
3. **Bei Problemen:** `check_harmony_versions.bat -force` - Forcierte Korrektur

## Fehlerbehebung

### Git nicht gefunden
```
ERROR: Git is not installed or not in PATH!
```
**Lösung:** Git von https://git-scm.com/ installieren

### Repository nicht erreichbar
```
ERROR: Failed to clone [repository]
```
**Lösungsmöglichkeiten:**
- Internetverbindung prüfen
- GitHub-Zugang testen
- Firewall/Proxy-Einstellungen prüfen
- SSH vs HTTPS klonen versuchen

### Falsche Version oder Version-Konflikt
```
✗ [repository]: commit-abc123 (expected v1.2.3)
```
**Lösung:**
```cmd
# Automatische Korrektur
check_harmony_versions.bat

# Falls uncommitted changes vorhanden
check_harmony_versions.bat -force
```

### Working Directory nicht sauber
```
⚠ Working directory has uncommitted changes
```
**Lösungsmöglichkeiten:**
1. Änderungen committen: `git add . && git commit -m "WIP"`
2. Force-Checkout: `check_harmony_versions.bat -force`
3. Manuell stashen: `git stash`

### PowerShell Execution Policy
```
execution of scripts is disabled on this system
```
**Lösung:**
```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

## Repository-Pfade

Die Skripte verwenden den Standard-MCC HarmonyContent Pfad:
- **Harmony Root:** `C:\Users\M91221\.mcc\HarmonyContent\`
- **Ziel:** MCC HarmonyContent Verzeichnis (wird automatisch von MPLAB X/MCC erkannt)

Dies entspricht dem Standard-Verzeichnis, das MPLAB Code Configurator für Harmony 3 Content verwendet.

## Integration mit VS Code

Nach dem Setup können Sie das Projekt in VS Code öffnen:

1. **Empfohlene Extensions installieren** (siehe Hauptprojekt-README)
2. **Python Environment einrichten:**
   ```powershell
   python -m venv .venv
   .venv\Scripts\Activate.ps1
   pip install -r requirements.txt
   ```
3. **Build ausführen** über VS Code Tasks oder MPLAB X Extension

## Version History

- **v1.0:** Basic Batch-Skript
- **v1.1:** Advanced Batch-Version mit Menü
- **v1.2:** PowerShell-Version mit modernen Features

## Support

Bei Problemen:
1. Repository-Status mit `-Status` prüfen
2. Clean Setup mit `-Clean` versuchen  
3. Git-Installation und Internetverbindung validieren
4. GitHub-Repository-Zugang testen