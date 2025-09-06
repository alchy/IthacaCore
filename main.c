#include <stdio.h>

/**
 * Hlavní funkce programu, která vypíše "hello world!" a zpracuje parametry z příkazové řádky.
 * Pokud jsou parametry poskytnuty, vypíše je na stdout.
 * @param argc Počet argumentů (včetně názvu programu).
 * @param argv Pole argumentů.
 * @return Vrací 0 při úspěšném ukončení programu.
 */
int main(int argc, char *argv[]) {
    printf("hello world!\n");
    
    // Zpracování parametrů, pokud existují
    if (argc > 1) {
        printf("Poskytnuté parametry: ");
        for (int i = 1; i < argc; i++) {
            printf("%s ", argv[i]);
        }
        printf("\n");
    } else {
        printf("Žádné parametry nebyly poskytnuty.\n");
    }
    
    return 0;
}