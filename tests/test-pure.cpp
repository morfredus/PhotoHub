// Tests des fonctions pures de PhotoHub (sans reseau ni interface).
#include "PsQuote.h"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond) do { if (!(cond)) { std::printf("ECHEC ligne %d : %s\n", __LINE__, #cond); \
    std::exit(1); } } while (0)

int main()
{
    using photohub::psQuote;
    CHECK(psQuote(QStringLiteral("C:/Users/Jean/Temp/x.ps1")) == QStringLiteral("C:/Users/Jean/Temp/x.ps1"));
    // Apostrophe doublee : sinon elle fermerait la chaine PowerShell.
    CHECK(psQuote(QStringLiteral("C:/Users/L'hote/x.ps1")) == QStringLiteral("C:/Users/L''hote/x.ps1"));
    // Tentative d'injection : l'apostrophe est neutralisee, pas le reste (inerte dans '...').
    CHECK(psQuote(QStringLiteral("a'; calc; '")) == QStringLiteral("a''; calc; ''"));
    CHECK(psQuote(QString()).isEmpty());
    std::puts("OK");
    return 0;
}
