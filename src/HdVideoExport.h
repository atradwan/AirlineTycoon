#pragma once
//============================================================================================
// HdVideoExport.h - H25a: Bilder der animierten Personen in Raeumen (CSmackerPerson) exportieren
//============================================================================================
#include <string>

// Exportiert die Smacker-Clips der Raeume Bild fuer Bild als RGBA-PNG (Index 0 = durchsichtig) nach
// <AppPath>hd_assets/original/video/<clip>.smk/<bild>.png, dazu je Clip frames.csv (Raeume, Groesse,
// Bounding-Box der deckenden Pixel je Bild). spec: "alle" oder eine Liste (Komma/Leerzeichen) aus Raeumen
// (Anfang des Namens genuegt, z. B. "duty") und Clips (z. B. "dukau" oder "dukau.smk").
void HdVideoExport(const std::string &spec);
