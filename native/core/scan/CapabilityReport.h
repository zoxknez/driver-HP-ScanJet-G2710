// Tabela mogucnosti kao JSON, na jednom mestu.
//
// Isti tekst citaju TRI potrosaca:
//
//   g2710ctl capabilities --json     dijagnostika i tools/generate-status.py
//   g2710_capabilities()             aplikacija, kroz C ABI
//   docs/STATUS.md                   generisano iz prvog
//
// Da svaki od njih ima svoj ispis, razisli bi se - i onda bi aplikacija
// pokazivala jedno, STATUS drugo, a izvestaj sa prijateljevog racunara trece.
// Zato ovde stoji jedna funkcija, a ne tri slicna `printf` bloka.
//
// Racun je statican i ne dodiruje uredjaj: radi i kada skenera nema, sto je i
// razlog zasto aplikacija moze pokazati sta se nudi pre nego sto se ista
// prikljuci.

#pragma once

#include <string>

namespace g2710::scan {

// Ceo izvestaj o mogucnostima, kao JSON.
//
// Oblik je ugovor - `tools/generate-status.py` ga cita, kao i .NET strana.
// Menja se namerno, i tada se menjaju i oba citaoca.
std::string capabilitiesJson();

}  // namespace g2710::scan
