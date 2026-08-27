/*
   Contains every user-visible PAKET message in one place. The active catalog
   is Slovenian and deliberately uses plain ASCII for CP/M terminal safety.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/messages.h"

static const char *const slovenian_messages[PAKET_MSG_COUNT] = {
    "Zanimiv",
    "Povprecen",
    "Odlicen",
    "Legendaren",
    "Neznan",
    "UPORABA",
    "  PAKET [-p vrata] [-c zveza] [-m bajti]          Prikazi katalog",
    "  PAKET [-p vrata] [-c zveza] [-m bajti] VZOREC   Isci po katalogu",
    "  PAKET [-p vrata] [-c zveza] [-m bajti] ID       Prikazi podrobnosti",
    "  PAKET [-p vrata] [-c zveza] [-m bajti] ID CILJ  Prenesi paket",
    "Vrata: 2=SIO1B (privzeto), 3=SIO2A/VAX, 4=SIO2B",
    "Zveza: 2400|4800|9600-N|E|O-1|2 (primer 2400-N-1)",
    "Brez -c velja privzeta zveza; -m doloca 16 do 112 bajtov.",
    "Cilji: A:/ = uporabnik 0, A:/1/ do A:/15/",
    "\nNapaka: %s\n",
    "\nNi paketov, ki ustrezajo zahtevi.",
    "\nSkupaj: %u paketov.\n",
    "Ime",
    "ID paketa",
    "Ponudnik",
    "Platforma",
    "Razlicica",
    "%u\n",
    "%s (%u/4)\n",
    "Za ta paket ni datotek za prenos.",
    "%u. ",
    "velikost ni znana",
    "%lu bajtov",
    "  ... in se %u moznosti\n",
    "Pri prenosu nadomestni znaki niso dovoljeni.",
    "Paket nima datotek za prenos.",
    "Napacen cilj; uporabite A:/ ali A:/0/ do A:/15/.",
    "Datoteke %s ni mogoce ustvariti.\n",
    "\nPrenos ni uspel: %s\n",
    "Nepopolna datoteka je bila odstranjena.",
    "\nPrenos je uspesno koncan.",
    "Napacna ali ponovljena moznost -p, -c oziroma -m.",
    "Povezava: Retro Vault prek serijskih vrat %u ... ",
    "Napaka: serijskih vrat %u ni mogoce odpreti.\n",
    "v redu",
    "napacen argument",
    "serijska povezava ni uspela",
    "napacen odgovor streznika",
    "paket ni najden",
    "oznaka ali zahteva je predolga",
    "zapis datoteke ni uspel",
    "neznana napaka",
    "napacna zahteva",
    "Retro Vault ni na voljo",
    "ni najdeno",
    "odgovor je prevelik",
    "napaka streznika",
    "neznano stanje streznika",
    "KATALOG PAKETOV",
    "REZULTATI ISKANJA",
    "ID PAKETA",
    "IME PAKETA",
    "Namig: PAKET <ID> prikaze vse podatke o izbranem paketu.",
    "Vzorec",
    "PODROBNOSTI PAKETA",
    "OPIS",
    "DATOTEKE ZA PRENOS",
    "Za prenos: PAKET %s A:/\n",
    "PRENOS PAKETA",
    "Cilj",
    "Format",
    "Velikost",
    "Datoteke",
    "Izbira",
    "Stanje",
    "prenos poteka",
    "preostalo: %lu bajtov",
    "povezano.\n",
    "ni uspela.\n",
    "POVEZAVA",
    "CILJ PRENOSA",
    "Leto izdaje",
    "Ocena",
    "\nSkupaj: %u paket.\n",
    "\nSkupaj: %u paketa.\n",
    "\nSkupaj: %u paketi.\n"
};

const char *paket_message(paket_message_id message_id)
{
    if ((message_id < 0) || (message_id >= PAKET_MSG_COUNT)) {
        return slovenian_messages[PAKET_MSG_STATUS_UNKNOWN];
    }
    return slovenian_messages[message_id];
}
