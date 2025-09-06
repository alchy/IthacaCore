Inicializace C++ projektu v VS Code
Tento projekt slouží jako základní inicializace pro C++ vývoj v Visual Studio Code s použitím CMake a Visual Studio 2022 Community Edition. Obsahuje jednoduchý Hello World program, který vypíše "hello world!" na standardní výstup a hlásí absenci parametrů.
Struktura projektu

CMakeLists.txt: Konfigurační soubor pro CMake, který definuje projekt.
main.cpp: Hlavní program v C++.
.vscode/tasks.json: Úlohy pro konfiguraci a sestavení projektu.
.vscode/launch.json: Konfigurace pro spuštění programu přes tlačítko „spustit“.

Požadavky

Visual Studio Code
Visual Studio 2022 Community Edition (s nainstalovanými C++ komponentami)
CMake (verze 3.10 nebo vyšší, přidejte do PATH)
Rozšíření v VS Code: CMake Tools a C/C++ od Microsoftu

Postup nastavení

Uložte soubory CMakeLists.txt a main.cpp do kořenové složky projektu.
Vytvořte složku .vscode a uložte do ní tasks.json a launch.json.
Vytvořte složku build v kořeni projektu (mkdir build).
Otevřete projekt v VS Code (File → Open Folder).
Spusťte CMake: Configure (Ctrl+Shift+P → CMake: Configure).

Sestavení a spuštění

Spusťte úlohu "build" (Ctrl+Shift+P → Tasks: Run Task → build) pro sestavení.
Klikněte na tlačítko spustit (|>) nebo stiskněte F5 pro spuštění.
Výstup programu: "hello world!\nŽádné parametry nebyly poskytnuty."

Poznámky

Cesta k vcvars64.bat v tasks.json a launch.json je nastavena na standardní pro Visual Studio 2022 Community: C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat. Pokud se liší, upravte ji.
Pro vyčištění smažte složku build.
Projekt je přenositelný a lze jej rozšířit pro složitější C++ aplikace.
