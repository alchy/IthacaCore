#include <stdio.h>
#include "sampler/core_logger.h"       // Include pro Logger
#include "sampler/sampler.h"           // Include pro runSampler

/**
 * Hlavní funkce programu, která vypíše úvodní zprávu, inicializuje logger
 * a předá řízení do sampleru.
 * 
 * ARCHITEKTURA VÝSTUPŮ:
 * - main.cpp: Používá pouze printf pro konzolový výstup
 * - Logger: Vypisuje inicializační zprávy na konzoli + loguje do souboru
 * - Všechny ostatní moduly: Pouze logování do souboru, žádné konzolové výstupy
 * 
 * CHOVÁNÍ PŘI CHYBÁCH:
 * - Pokud Logger selže při inicializaci, program se ukončí s exit(1)
 * - Pokud jakýkoliv další modul selže, program se ukončí s exit(1)
 * - Všechny chyby jsou logovány před ukončením
 * 
 * @param argc Počet argumentů (včetně názvu programu).
 * @param argv Pole argumentů.
 * @return Vrací 0 při úspěšném ukončení programu.
 */
int main(int argc, char *argv[]) {
    // Úvodní banner - pouze printf na konzoli
    printf("=====================================\n");
    printf("      IthacaCore Audio Sampler      \n");
    printf("      Professional Sample Engine     \n");
    printf("=====================================\n");
    printf("[main] Starting IthacaCore application\n");

    // Zpracování parametrů příkazové řádky
    if (argc > 1) {
        printf("[main] Command line parameters detected: ");
        for (int i = 1; i < argc; i++) {
            printf("%s ", argv[i]);
        }
        printf("\n");
        printf("[main] Parameters will be passed to sampler system\n");
    } else {
        printf("[main] No command line parameters provided\n");
        printf("[main] Using default configuration\n");
    }

    printf("-------------------------------------\n");
    printf("[main] Initializing logger subsystem\n");
    
    // Inicializace loggeru s cestou "./" (aktuální adresář)
    // Logger bude vypisovat své vlastní inicializační zprávy na konzoli
    // Od tohoto bodu bude veškeré logování modulů probíhat pouze přes Logger
    Logger logger("./");
    
    // Logger inicializace dokončena - pokračujeme
    printf("-------------------------------------\n");
    printf("[main] Logger initialization completed\n");
    printf("[main] Transferring control to sampler system\n");
    printf("[main] All further operations will be logged to: core_logger/core_logger.log\n");
    printf("[main] Monitor log file for detailed operation progress\n");
    printf("=====================================\n\n");

    // Předání řízení do sampleru s loggerem
    // Od tohoto bodu budou všechny operace logovány pouze do souboru
    // Žádné další konzolové výstupy kromě závěrečných zpráv z main.cpp
    int result = runSampler(logger);
    
    // Závěrečný výstup na konzoli - pouze main.cpp
    printf("\n=====================================\n");
    printf("[main] Sampler system execution completed\n");
    
    if (result == 0) {
        printf("[main] SUCCESS: All operations completed successfully\n");
        printf("[main] Exit code: 0 (success)\n");
    } else {
        printf("[main] WARNING: System completed with errors\n");
        printf("[main] Exit code: %d (check logs for details)\n", result);
    }
    
    printf("[main] Detailed operation log available in the displayed log file path above\n");
    printf("[main] Thank you for using IthacaCore Audio Sampler\n");
    printf("=====================================\n");
    
    return result;
}