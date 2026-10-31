// ============================================================================
//  Grad no! – Halterung im Stil eines Haltestellen-Masts
//  für das "Cheap Yellow Display" ESP32-2432S028R (2,8", 86 × 50 mm)
//
//  Teile (Variable "teil" setzen oder per Kommandozeile: -D 'teil="rahmen"'):
//    "zusammenbau"  – Vorschau des ganzen Halters (nicht drucken)
//    "rahmen"       – Front-Rahmen mit Displayfenster, "H" vertieft (Front nach unten drucken)
//    "rahmen_ohne_h"  – Front-Rahmen ohne "H"
//    "rahmen_2farbig" + "h_einlage" – Rahmen und "H"-Einlage für den 2-Farben-Druck (zusammen laden)
//    "rueckteil"    – Rückschale mit Mastkopf             (Rückseite nach unten)
//    "mast"         – Mast (Rohr mit Kabelkanal)          (liegend drucken)
//    "verbinder"    – 2 Steckverbinder Mast <-> Kopf/Sockel (stehend drucken)
//    "sockel"       – Sockel mit USB-Buchse               (Oberseite nach unten)
//    "boden"        – Bodenplatte für den Sockel
//    "passtest"     – dünne Testplatte: prüft Fenster und Schraublöcher (10 min Druck)
//
//  Kabel: Verlängerung "USB-C-Einbaubuchse auf Stecker passend zum Board, 90° gewinkelt",
//  ca. 30 cm. Der Stecker steckt im Display, das Kabel läuft durch den hohlen Mast
//  in den Sockel, die Buchse schaut seitlich aus dem Sockel.
//
//  Alle Maße in mm. Werte unter "Board" bitte einmal mit dem eigenen Board vergleichen
//  (siehe README im Ordner gehaeuse/) und die Passtest-Platte drucken.
// ============================================================================

teil = "zusammenbau";

/* [Board – bitte nachmessen] */
pcb_b      = 86.0;   // Platinenbreite (lange Seite)
pcb_h      = 50.0;   // Platinenhöhe
pcb_d      = 1.6;    // Platinendicke
loch_rand  = 4.0;    // Mitte der Befestigungslöcher vom Platinenrand (alle 4 Ecken)
lcd_d      = 4.0;    // Höhe des Displays über der Platinen-Vorderseite
// sichtbarer Bildbereich, gemessen von der Platinenkante, USB-Buchse RECHTS:
bild_links = 10.4;   // linke Platinenkante -> linker Rand des Bildbereichs
bild_unten = 3.4;    // untere Platinenkante -> unterer Rand des Bildbereichs
bild_b     = 57.6;   // Breite des Bildbereichs (2,8": 57,6)
bild_h     = 43.2;   // Höhe des Bildbereichs  (2,8": 43,2)
usb_hoehe  = 20.0;   // untere Platinenkante -> Mitte USB-Buchse (USB rechts)
hinten_frei = 11.9;  // Platz hinter der Platine (Bauteile + gewinkelter Stecker); 11,9 -> Schild genau so tief wie der Mast

/* [Gehäuse] */
wand       = 2.4;    // Wandstärke
front      = 2.0;    // Stärke der Frontplatte
spiel      = 0.6;    // Luft rund um die Platine
lcd_luft   = 0.5;    // Abstand Frontplatte <-> Display (damit der Touch nicht dauernd gedrückt wird)
fenster_zu = 0.8;    // Fenster ist so viel größer als der Bildbereich (je Seite)
usb_platz  = 18.0;   // Platz links neben der Platine für den gewinkelten USB-Stecker
r_ecke     = 3.0;    // Eckenradius
h_modus    = 1;      // Haltestellen-"H" neben dem Display: 0 = ohne, 1 = vertieft (einfarbig), 2 = für 2-Farben-Druck
h_tiefe    = 0.8;    // Tiefe des "H" (bei 2 Farben = Dicke der Einlage, 4 Schichten à 0,2 mm)

/* [Mast & Sockel] */
mast       = 22;     // Mast-Querschnitt (quadratisch)
kanal_x    = 13.0;   // Kabelkanal im Mast (Breite)
kanal_y    = 9.5;    // Kabelkanal im Mast (Tiefe) – der Stecker muss durchpassen!
zapfen_l   = 12;     // Länge der Steckzapfen
passung    = 0.3;    // Spiel für Steckverbindungen
mast_l     = 150;    // Länge des Mast-Mittelstücks
mastkopf_oben = 15;  // so weit ragt der Mast über das Schild hinaus
sockel_l   = 140;
sockel_t   = 85;
sockel_h   = 22;
mast_pos   = 35;     // Mastmitte vom linken Sockelrand
// USB-C-Einbaubuchse (Gehäuse der Kabelbuchse, wird im Sockel eingeklemmt)
buchse_b   = 13.0;   // Breite des Buchsengehäuses
buchse_h   = 8.5;    // Höhe des Buchsengehäuses
buchse_l   = 22.0;   // Länge des Buchsengehäuses
port_b     = 10.0;   // Öffnung in der Sockelwand
port_h     = 4.4;

$fn = 48;
e = 0.01;

// ------------------------------------------------------------ abgeleitete Maße
innen_b  = usb_platz + pcb_b + spiel;        // innen, x
innen_h  = pcb_h + 2*spiel;                  // innen, z
box_b    = innen_b + 2*wand;
box_h    = innen_h + 2*wand;
pcb_x0   = wand + usb_platz;                 // Platine (gedreht: USB links)
pcb_z0   = wand + spiel;
pcb_vorne = front + lcd_luft + lcd_d;        // y der Platinen-Vorderseite
pcb_hinten = pcb_vorne + pcb_d;
box_t    = pcb_hinten + hinten_frei + front; // Gesamttiefe
trenn_y  = pcb_hinten + 1.2;                 // Trennung Rahmen / Rückteil
falz     = 2.0;                              // Überlappung Rahmen -> Rückteil
falz_b   = wand / 2;                         // Breite des Falzes (innere Wandhälfte)

// Bildbereich nach 180°-Drehung (USB links), in Gehäusekoordinaten
fx0 = pcb_x0 + (pcb_b - bild_links - bild_b) - fenster_zu;
fz0 = pcb_z0 + (pcb_h - bild_unten - bild_h) - fenster_zu;
fb  = bild_b + 2*fenster_zu;
fh  = bild_h + 2*fenster_zu;

// Lochpositionen (symmetrisch, daher unabhängig von der Drehung)
loecher = [for (ix=[0,1], iz=[0,1])
  [pcb_x0 + (ix==0 ? loch_rand : pcb_b - loch_rand),
   pcb_z0 + (iz==0 ? loch_rand : pcb_h - loch_rand)]];

// Kabelfenster zwischen Mastkopf und Schild (Höhe der USB-Buchse nach Drehung)
usb_z      = pcb_z0 + (pcb_h - usb_hoehe);
kfenster_h = 30;
kfenster_z0 = max(zapfen_l + 4, usb_z - kfenster_h/2);

// Mast liegt links am Schild, mittig in der Tiefe
mast_x0 = -mast;
// Rückseite von Mastkopf und Schild bündig, damit das Rückteil plan auf dem Druckbett liegt
// (auch wenn Mast und Schild durch geänderte Maße unterschiedlich tief werden)
mast_y0 = box_t - mast;
kanal_cx = mast_x0 + mast/2;
kanal_cy = mast_y0 + mast/2;
zapfen_x = kanal_x + 4;
zapfen_y = kanal_y + 4;

// ------------------------------------------------------------ Hilfsformen
module rrect(b, h, r) {   // abgerundetes Rechteck in XY, Ursprung links unten
  r2 = min(r, b/2-e, h/2-e);
  hull() for (x=[r2, b-r2], y=[r2, h-r2]) translate([x,y]) circle(r2);
}
// Quader mit gerundeten Kanten in X/Z-Ebene, Tiefe in Y
module rbox_xz(b, h, t, r) {
  rotate([-90,0,0]) translate([0,-h,0]) linear_extrude(t) rrect(b, h, r);
}
module kanal_quer(l, extra=0) { // Kabelkanal entlang Z, zentriert auf Mastachse
  translate([kanal_cx - (kanal_x+extra)/2, kanal_cy - (kanal_y+extra)/2, 0])
    cube([kanal_x+extra, kanal_y+extra, l]);
}
module zapfen_form(l, extra=0) {
  translate([kanal_cx - (zapfen_x+extra)/2, kanal_cy - (zapfen_y+extra)/2, 0])
    linear_extrude(l) rrect(zapfen_x+extra, zapfen_y+extra, 1);
}

// ------------------------------------------------------------ Rahmen (vorne)
module rahmen(hm = h_modus) {
  difference() {
    union() {
      // Frontplatte + Seitenwände bis zur Trennlinie
      difference() {
        rbox_xz(box_b, box_h, trenn_y, r_ecke);
        translate([wand, front, wand]) cube([innen_b, trenn_y, innen_h]);
      }
      // Falz: innere Hälfte der Wand ragt umlaufend ins Rückteil.
      // Er steht direkt auf der Wand (1,2 mm breit, 1 mm in die Wand hinein verschmolzen) und hält dadurch sicher.
      translate([0, trenn_y - 1, 0]) difference() {
        translate([wand - falz_b, 0, wand - falz_b]) rbox_xz(innen_b + 2*falz_b, innen_h + 2*falz_b, falz + 1, 1);
        translate([wand, -e, wand]) cube([innen_b, falz + 1 + 2*e, innen_h]);
      }
      // Schraubdome für die Platine
      for (p = loecher) translate([p[0], front - e, p[1]])
        rotate([-90,0,0]) cylinder(d=6.0, h=lcd_luft + lcd_d + e);
    }
    // Displayfenster mit Fase nach vorne
    translate([fx0, -e, fz0]) rbox_xz(fb, fh, front + 2*e, 1);
    hull() {
      translate([fx0, front*0.45, fz0]) rbox_xz(fb, fh, e, 1);
      translate([fx0-1.2, -e, fz0-1.2]) rbox_xz(fb+2.4, fh+2.4, e, 2);
    }
    // Schraubenlöcher (M3, selbstschneidend)
    for (p = loecher) translate([p[0], front - 1.2, p[1]])
      rotate([-90,0,0]) cylinder(d=2.6, h=20);
    // Kabelfenster (hinterer Teil der linken Wand)
    kabelfenster();
    // Haltestellen-"H" auf der freien Fläche neben dem Display (vertieft bzw. Platz für die Einlage)
    if (hm > 0) translate([0, -e, 0]) h_form(h_tiefe + e);
  }
}

// "H" im Kreis (wie das Haltestellenschild), in Rahmen-Koordinaten, von der Front aus t tief
h_d = min(fx0 - 8, box_h - 14);
module h_form(t) {
  translate([(wand + fx0)/2, 0, box_h/2]) rotate([-90,0,0])
    linear_extrude(t) {
      difference() { circle(d=h_d, $fn=96); circle(d=h_d - 3.6, $fn=96); }
      text("H", size=h_d*0.46, halign="center", valign="center", font="Liberation Sans:style=Bold");
    }
}
// Einlage für den 2-Farben-Druck: füllt die Vertiefung genau aus
module h_einlage() { h_form(h_tiefe); }

// ------------------------------------------------------------ Rückteil + Mastkopf
module kabelfenster() {
  translate([-mast/2, kanal_cy - kanal_y/2, kfenster_z0])
    cube([mast/2 + wand + 1, kanal_y, kfenster_h]);
}

module mastkopf() {
  kopf_h = box_h + mastkopf_oben;
  difference() {
    union() {
      translate([mast_x0, mast_y0, 0]) linear_extrude(kopf_h) rrect(mast, mast, 2);
      // Spitze
      translate([mast_x0 + mast/2, mast_y0 + mast/2, kopf_h - e])
        rotate([0,0,45]) cylinder(d1=mast*1.414, d2=4, h=7, $fn=4);
    }
    // Steckbuchse unten für das Mast-Mittelstück
    translate([0,0,-e]) zapfen_form(zapfen_l + passung + e, 2*passung);
    // Kabelkanal bis zum Kabelfenster
    kanal_quer(kfenster_z0 + kfenster_h);
    kabelfenster();
  }
}

module rueckteil() {
  difference() {
    union() {
      // Schale
      translate([0, trenn_y, 0]) rbox_xz(box_b, box_h, box_t - trenn_y, r_ecke);
      mastkopf();
    }
    // Innenraum
    translate([wand, trenn_y - e, wand]) cube([innen_b, box_t - trenn_y - front + e, innen_h]);
    // Falz-Aufnahme (innere Wandhälfte ausgespart, 0,15 mm Spiel)
    translate([wand - falz_b - 0.15, trenn_y - e, wand - falz_b - 0.15])
      cube([innen_b + 2*falz_b + 0.3, falz + 0.3, innen_h + 2*falz_b + 0.3]);
    kabelfenster();
    // Schraublöcher M3 (durchgehend, mit Senkung für den Kopf)
    for (p = loecher) translate([p[0], 0, p[1]]) rotate([-90,0,0]) {
      cylinder(d=3.3, h=box_t + 1);
      translate([0,0,box_t - 3.5]) cylinder(d=6.2, h=4);   // versenkter Kopf -> Schraube M3x16
    }
    // Lüftungsschlitze
    for (i=[0:5]) translate([pcb_x0 + 20 + i*9, box_t - front - e, wand + 4])
      rbox_xz(3, 10, front + 2*e, 1.5);
    // Schriftzug hinten
    translate([pcb_x0 + pcb_b/2, box_t - 0.6, box_h - wand - 12]) rotate([90,0,180])
      linear_extrude(1) text("Grad no!", size=7, halign="center",
                             font="Liberation Sans:style=Bold");
  }
  // Distanzhülsen hinten (drücken die Platine gegen die Dome im Rahmen)
  for (p = loecher) translate([p[0], pcb_hinten + 0.2, p[1]]) rotate([-90,0,0])
    difference() {
      cylinder(d=7.6, h=box_t - front - pcb_hinten - 0.2 + e);
      translate([0,0,-e]) cylinder(d=3.3, h=box_t);
    }
}

// ------------------------------------------------------------ Mast + Verbinder
// Der Mast ist ein Rohr mit Steckbuchsen an beiden Enden. Zwei kurze Verbinder
// (Doppelzapfen) verbinden ihn mit Mastkopf und Sockel – so druckt sich alles ohne Support.
module mast_mitte() {
  difference() {
    translate([mast_x0, mast_y0, 0]) linear_extrude(mast_l) rrect(mast, mast, 2);
    translate([0,0,-e]) kanal_quer(mast_l + 2*e);
    translate([0,0,-e]) zapfen_form(zapfen_l + passung + e, 2*passung);
    translate([0,0,mast_l - zapfen_l - passung]) zapfen_form(zapfen_l + passung + e, 2*passung);
  }
}

module verbinder() {   // Doppelzapfen, 2 Stück drucken
  difference() {
    zapfen_form(2*zapfen_l);
    translate([0,0,-e]) kanal_quer(2*zapfen_l + 2*e);
  }
}

// ------------------------------------------------------------ Sockel
// Sockel-Koordinaten: Mastachse liegt bei (kanal_cx, kanal_cy)
sx0 = kanal_cx - mast_pos;
sy0 = kanal_cy - sockel_t/2;
buchse_z = 10;   // Mitte der Buchse über der Unterkante
boden_d  = 2.4;  // Dicke der Bodenplatte (liegt bündig im Sockel)

module sockel_grundform(h) {
  hull() {
    translate([sx0, sy0, 0]) linear_extrude(h - 3) rrect(sockel_l, sockel_t, 6);
    translate([sx0 + 2, sy0 + 2, 0]) linear_extrude(h) rrect(sockel_l - 4, sockel_t - 4, 4);
  }
}

module sockel() {
  difference() {
    union() {
      difference() {
        sockel_grundform(sockel_h);
        translate([sx0 + wand, sy0 + wand, -e])
          linear_extrude(sockel_h - 3 + e) rrect(sockel_l - 2*wand, sockel_t - 2*wand, 4);
      }
      // Block für die Mast-Aufnahme
      translate([kanal_cx - mast/2, kanal_cy - mast/2, sockel_h - zapfen_l - 4])
        cube([mast, mast, zapfen_l + 4]);
      // Halter für die USB-Buchse
      translate([sx0 + wand - e, kanal_cy - buchse_b/2 - 2.4, boden_d])
        cube([buchse_l + 2.4, buchse_b + 4.8, buchse_z + buchse_h/2 + 2.4 - boden_d]);
      // Schraubdome für die Bodenplatte
      for (p = boden_schrauben()) translate([p[0], p[1], boden_d]) cylinder(d=8, h=sockel_h - 3 - boden_d + e);
    }
    // Mast-Aufnahme + Kabeldurchgang
    translate([0,0,sockel_h - zapfen_l - passung]) zapfen_form(zapfen_l + passung + e, 2*passung);
    translate([0,0,sockel_h - zapfen_l - 5]) kanal_quer(10);
    // Tasche für das Buchsengehäuse (von unten eingelegt)
    translate([sx0 + wand - 2*e, kanal_cy - buchse_b/2, -e])
      cube([buchse_l + 2*e, buchse_b, buchse_z + buchse_h/2 + e]);
    // Kabelausgang der Tasche nach innen
    translate([sx0 + wand + buchse_l - e, kanal_cy - 3.5, -e]) cube([3, 7, buchse_z + 3.5]);
    // Öffnung für den Stecker in der Seitenwand
    translate([sx0 - e, kanal_cy - port_b/2, buchse_z - port_h/2])
      rotate([90,0,90]) linear_extrude(wand + 4*e) rrect(port_b, port_h, 1.6);
    // Schraublöcher
    for (p = boden_schrauben()) translate([p[0], p[1], -e]) cylinder(d=2.6, h=12);
  }
}

function boden_schrauben() = [
  [sx0 + 8, sy0 + 8], [sx0 + sockel_l - 8, sy0 + 8],
  [sx0 + 8, sy0 + sockel_t - 8], [sx0 + sockel_l - 8, sy0 + sockel_t - 8]];

module boden() {
  difference() {
    union() {
      translate([sx0 + wand + 0.3, sy0 + wand + 0.3, 0])
        linear_extrude(boden_d) rrect(sockel_l - 2*wand - 0.6, sockel_t - 2*wand - 0.6, 4);
      // Steg, der das Buchsengehäuse von unten hält
      translate([sx0 + wand + 0.5, kanal_cy - buchse_b/2 + 0.3, boden_d - e])
        cube([buchse_l - 1, buchse_b - 0.6, buchse_z - buchse_h/2 - 0.2 - boden_d]);
    }
    for (p = boden_schrauben()) translate([p[0], p[1], -e]) {
      cylinder(d=3.4, h=5);
      cylinder(d1=6.5, d2=3.4, h=1.6);
    }
    // Vertiefungen für Gummifüße (Ø 10 mm)
    for (p = boden_schrauben()) translate([p[0] + (p[0] > kanal_cx ? -14 : 14), p[1], -e])
      cylinder(d=10.5, h=1);
  }
}
// Hinweis: Die Bodenplatte liegt im Sockel bündig auf den Schraubdomen.
// Im Hohlraum ist Platz für Ballast (z.B. Muttern oder Unterlegscheiben einkleben).

// ------------------------------------------------------------ Passtest-Platte
module passtest() {
  difference() {
    translate([pcb_x0 - 3, 0, pcb_z0 - 3]) rbox_xz(pcb_b + 6, pcb_h + 6, 1.2, 2);
    translate([fx0, -e, fz0]) rbox_xz(fb, fh, 2, 1);
    for (p = loecher) translate([p[0], -e, p[1]]) rotate([-90,0,0]) cylinder(d=3.2, h=3);
    // Markierung auf der USB-Seite
    translate([pcb_x0 + 2, -e, pcb_z0 + pcb_h/2 - 4]) cube([2, 3, 8]);
  }
}

// ------------------------------------------------------------ Zusammenbau
module zusammenbau() {
  kopf_z = sockel_h + mast_l;   // Mastkopf sitzt oben auf dem Mast
  color("#f2f2f2") sockel();
  color("#c8c8c8") translate([0,0,sockel_h - zapfen_l]) verbinder();
  color("#d9d9d9") translate([0,0,sockel_h]) mast_mitte();
  color("#c8c8c8") translate([0,0,kopf_z - zapfen_l]) verbinder();
  translate([0,0,kopf_z]) {
    color("#f2f2f2") rueckteil();
    color("#f2f2f2") rahmen();
    // Platine + Display als Platzhalter
    color("#e8a33d") translate([pcb_x0, pcb_vorne, pcb_z0]) cube([pcb_b, pcb_d, pcb_h]);
    color("#1d2233") translate([fx0 + fenster_zu, front + lcd_luft, fz0 + fenster_zu])
      cube([bild_b, lcd_d, bild_h]);
  }
}

// ------------------------------------------------------------ Druckausrichtung
if (teil == "zusammenbau") zusammenbau();
else if (teil == "rahmen")    rotate([90,0,0]) rahmen();                       // Front auf dem Druckbett
else if (teil == "rahmen_ohne_h") rotate([90,0,0]) rahmen(0);
else if (teil == "rahmen_2farbig") rotate([90,0,0]) rahmen(2);
else if (teil == "h_einlage") rotate([90,0,0]) h_einlage();                // gleiche Lage wie der Rahmen
else if (teil == "rueckteil") rotate([-90,0,0]) translate([0,-box_t,0]) rueckteil(); // Rückseite auf dem Druckbett
else if (teil == "mast")      rotate([0,-90,0]) translate([-mast_x0, -mast_y0, -mast_l]) mast_mitte(); // liegend
else if (teil == "verbinder") for (i=[0,1]) translate([i*25, 0, 0]) translate([-kanal_cx, -kanal_cy, 0]) verbinder();
else if (teil == "sockel")    mirror([0,0,1]) translate([0,0,-sockel_h]) sockel();   // Oberseite auf dem Druckbett
else if (teil == "boden")     boden();
else if (teil == "passtest")  rotate([90,0,0]) passtest();
