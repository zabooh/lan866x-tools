# T1S 100BaseT Bridge Firmware

## VS Code Setup für neue Checkouts

Nach dem Checkout des Repository folgende Schritte ausführen:

### 1. Empfohlene Extensions installieren
VS Code wird automatisch die Installation der empfohlenen Extensions vorschlagen:
- MPLAB Extension für VS Code
- Python Extension
- C/C++ Extension
- CMake Tools

### 2. Python Virtual Environment einrichten
```powershell
# Im Projektverzeichnis
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

### 3. Build ausführen
- Mit VS Code: `Ctrl+Shift+P` → "Tasks: Run Build Task" → "MPLAB X: Clean and Build"
- Oder über Terminal: Die MPLAB X Extension sollte die Build-Commands bereitstellen

### 4. Testing
Die verschiedenen Python Test-Skripts verwenden COM8 als Standard-Port. Bei Bedarf anpassen.

## Wichtige Dateien

- `.vscode/tasks.json` - Build-Tasks für VS Code
- `.vscode/extensions.json` - Empfohlene Extensions
- `.vscode/c_cpp_properties.json` - C/C++ IntelliSense Konfiguration
- `requirements.txt` - Python Dependencies  
- `T1S_100BaseT_Bridge.mc3` - MPLAB X Projektkonfiguration
- `cmake/` - CMake Build-Konfiguration (wird von MPLAB X generiert, aber ins Git committed für Cross-Platform Builds)

## Build-System

Das Projekt verwendet CMake über die MPLAB X Integration. Die CMake-Dateien werden automatisch generiert, sind aber ins Git-Repository committed, damit das Projekt direkt nach dem Checkout buildbar ist, auch ohne MPLAB X zu öffnen.