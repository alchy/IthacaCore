# Grab-Files.ps1
# Tento skript rekurzivně prohledává aktuální adresář pro soubory se specifikovanými příponami,
# vylučuje adresáře začínající tečkou (např. .venv) a specifikované vyloučené adresáře,
# ale umožňuje include specifických adresářů i souborů.
# Obsah souborů spojuje do jednoho výstupního souboru s hlavičkou pro každý soubor,
# obsahující název souboru a jeho původní cestu.
# Výstupní soubor je pojmenován po aktuálním adresáři s časovou značkou ve formátu YYYYMMDDHHMM a příponou .txt.

# Parametry skriptu
Param(
    # Pole adresářů k vyloučení (výchozí: DESIGNS, JUCE, build).
    # Soubory v těchto adresářích (nebo podadresářích) nebudou zahrnuty, pokud nejsou v IncludeDirectories.
    [string[]]$ExcludeDirectories = @("DESIGNS", "JUCE", "build", "decorators", "libsndfile"),
    
    # Pole názvů adresářů k zahrnutí (výchozí: prázdné).
    # Tyto adresáře se zpracují i když začínají tečkou nebo jsou v ExcludeDirectories.
    [string[]]$IncludeDirectories = @(".vscode"),
    
    # Pole cest k souborům k zahrnutí (relativním k aktuálnímu adresáři, výchozí: prázdné).
    # Tyto soubory se přidají navíc, i když neodpovídají standardním příponám v $extensions.
    [string[]]$IncludeFiles = @()
)

# Konfigurace: Upravte pole $extensions pro přidání nebo odebrání typů souborů.
# Příklad: Pro C++ soubory přidejte '.h', '.cpp', 'makefile' atd.
$extensions = @('*.py', '*.json', '*.md', '*.cpp', '*.h', '*.txt')

# Získání názvu aktuálního adresáře
$currentDir = (Get-Item -Path .).Nam

# Generování časové značky
$timestamp = Get-Date -Format "yyyyMMddHHmm"

# Název výstupního souboru
$outputFile = "$currentDir$timestamp.txt"

# Vymazání nebo vytvoření výstupního souboru
Out-File -FilePath $outputFile -Encoding utf8

# Funkce pro rekurzní sběr souborů s vyloučením adresářů
function Get-FilesRecursively {
    <#
    .SYNOPSIS
    Rekurzivně sbírá soubory se specifikovanými příponami z daného adresáře,
    přeskakuje vyloučené adresáře a ty začínající tečkou, ale zahrnuje include adresáře.
    .PARAMETER Path
    Cesta k prohledávanému adresáři (výchozí: aktuální).
    .PARAMETER Extensions
    Pole přípon souborů k zahrnutí.
    .PARAMETER ExcludeDirectories
    Pole názvů adresářů k vyloučení.
    .PARAMETER IncludeDirectories
    Pole názvů adresářů k zahrnutí (přepíše exclude a tečkové pravidlo).
    .OUTPUTS
    Kolekce souborů (FileInfo objekty).
    #>
    Param(
        [string]$Path = ".",
        [string[]]$Extensions,
        [string[]]$ExcludeDirectories,
        [string[]]$IncludeDirectories
    )
    
    # Debug: Vypisuje prohledávaný adresář
    Write-Host "Prohledávám adresář: $Path"
    
    # Získání položek v aktuálním adresáři
    $items = Get-ChildItem -Path $Path
    
    # Inicializace prázdného pole pro soubory
    $collectedFiles = @()
    
    foreach ($item in $items) {
        if ($item.PSIsContainer) {
            # Je to adresář: Kontrola, zda není ignorovaný
            $dirName = $item.Name
            
            # Ignorovat tečkové adresáře, pokud nejsou v IncludeDirectories
            if ($dirName.StartsWith(".") -and $IncludeDirectories -notcontains $dirName) {
                continue
            }
            
            # Ignorovat vyloučené adresáře, pokud nejsou v IncludeDirectories
            if ($ExcludeDirectories -contains $dirName -and $IncludeDirectories -notcontains $dirName) {
                continue
            }
            
            # Rekurzní volání pro podadresář
            $collectedFiles += Get-FilesRecursively -Path $item.FullName -Extensions $Extensions -ExcludeDirectories $ExcludeDirectories -IncludeDirectories $IncludeDirectories
        } else {
            # Je to soubor: Kontrola přípony
            if ($Extensions -contains "*$($item.Extension)") {
                # Dodatečná kontrola: Vyloučit soubory odpovídající patternu výstupních souborů (aktuální adresář + 12 číslic + .txt)
                if (-not ($item.Name -match "^$currentDir\d{12}\.txt$")) {
                    $collectedFiles += $item
                }
            }
        }
    }
    return $collectedFiles
}

# Sběr souborů pomocí rekurzní funkce (standardní soubory)
$files = Get-FilesRecursively -Path . -Extensions $extensions -ExcludeDirectories $ExcludeDirectories -IncludeDirectories $IncludeDirectories

# Přidání include souborů (pokud existují a nejsou už zahrnuty)
foreach ($includePath in $IncludeFiles) {
    $fullIncludePath = Join-Path -Path (Get-Location) -ChildPath $includePath
    if (Test-Path -Path $fullIncludePath -PathType Leaf) {
        $includeFile = Get-Item -Path $fullIncludePath
        # Přidat pouze pokud není už v seznamu (duplicitní kontrola)
        if ($files.FullName -notcontains $includeFile.FullName) {
            $files += $includeFile
            Write-Host "Přidávám include soubor: $($includeFile.FullName)"
        }
    } else {
        Write-Host "Include soubor nenalezen: $includePath"
    }
}

# Zpracování všech souborů (standard + include)
foreach ($file in $files) {
    # Debug: Vypisuje přidávaný soubor
    Write-Host "Přidávám soubor: $($file.FullName)"
    
    # Hlavička pro soubor
    $header = @"
===== File: $($file.Name) =====
Path: $($file.FullName)
=====
"@
    
    # Přidání hlavičky do výstupu
    Add-Content -Path $outputFile -Value $header -Encoding utf8
    
    # Přidání obsahu souboru
    Get-Content -Path $file.FullName | Add-Content -Path $outputFile -Encoding utf8
    
    # Volitelně: Přidání prázdného řádku jako oddělovače
    Add-Content -Path $outputFile -Value "`n" -Encoding utf8
}

Write-Host "Soubory spojeny do $outputFile"