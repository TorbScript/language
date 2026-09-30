---
title: Wie schnell ist TorbScript?
summary: Sechs bekannte Programme, jedes in TorbScript, C, Python und JavaScript, jede Nacht auf derselben Maschine gemessen - ein mit torb build gebautes TorbScript-Programm gegen C, und torb run gegen Python und Node.js.
kind: site
status: stable
order: 40
translates: 41586aa929d2
---

TorbScript führt ein Programm auf zwei Arten aus. `torb build` macht daraus eine eigene Programmdatei, über C und einen
C-Compiler. `torb run` startet es sofort, in der VM - einem Interpreter in `torb`, der keinen Compiler braucht.

Diese Seite misst beides. Die Programme sind sechs aus dem
[Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/), der Sammlung, mit
der man Sprachen vergleicht, und jedes läuft gegen dasselbe Programm in C, Python und JavaScript.

<div data-benchmark="summary"></div>

In jedem Diagramm ist ein Balken die Zeit, die ein Programm gebraucht hat, im Verhältnis zu C auf derselben Eingabe.
**1× ist so schnell wie C, und 2× hat doppelt so lange gebraucht.** TorbScripts Binary ist der rote Balken.

## Programm für Programm

Zu jedem Programm steht hier, was es misst und wo TorbScript Arbeit macht, die das C nicht macht - denn dort fällt es
zurück. Das C ist das schlichte Programm des Benchmarks Game mit einem Thread, nicht sein schnellstes, und das
TorbScript folgt ihm Anweisung für Anweisung. Was sich hier zeigt, ist Arbeit für den Compiler: Nichts davon ist eine
Eigenschaft der Sprache, und [Performance](../PERFORMANCE.md#43-the-benchmarks-game) hat die Einzelheiten.

### `binary-trees`

Baut Millionen kleiner Bäume, läuft sie ab und wirft sie weg: Speicher anfordern und freigeben. Ein Knoten in C ist ein
`malloc` und ein `free`. Ein Knoten in TorbScript ist derselbe Block mit einem Referenzzähler darin, der beim Bauen und
Ablaufen des Baums hoch- und heruntergezählt wird, und der Baum wird freigegeben, wenn ihn nichts mehr benutzt.

<div data-benchmark="binary-trees"></div>

### `fannkuch-redux`

Dreht den Anfang kurzer Zahlenlisten um, wieder und wieder, für jede Reihenfolge der Zahlen: eine Liste über ihren
Index lesen und schreiben. TorbScript prüft jeden Index gegen die Länge der Liste und jede Summe auf Überlauf; das C
prüft beides nicht. Und jedes `permutation[i] = value` ist ein Aufruf in TorbScripts Laufzeit, in die der C-Compiler
nicht hineinsieht, wo das C in ein Array schreibt.

<div data-benchmark="fannkuch-redux"></div>

### `n-body`

Bewegt fünf Planeten in kleinen Schritten: Rechnen mit Dezimalzahlen, auf den Feldern von Records, die in einer Liste
liegen. Hier liegt TorbScript heute am weitesten zurück. Ein Planet sind sieben Zahlen, mehr als die 32 Bytes, die ein
Record haben darf, um direkt in der Liste zu liegen; die Liste hält also einen Zeiger auf ihn - und
`bodies[i].velocityX = ...` kopiert den ganzen Planeten, bevor es ein Feld davon ändert, wo das C das Feld über einen
Zeiger ändert.

<div data-benchmark="n-body"></div>

### `spectral-norm`

Multipliziert einen Vektor mit einer Matrix, deren Einträge unterwegs berechnet werden: eine Division und ein paar
Multiplikationen pro Schritt, in zwei Schleifen über Listen von Dezimalzahlen. Wie bei fannkuch-redux ist jedes
`product[i] = ...` ein Aufruf in die Laufzeit, und jeder Index wird geprüft.

<div data-benchmark="spectral-norm"></div>

### `mandelbrot`

Berechnet die Mandelbrot-Menge Punkt für Punkt und schreibt sie als Bitmap: Rechnen mit Dezimalzahlen in einer engen
Schleife, und sonst nichts. Das ist das Programm, bei dem die beiden Sprachen dem C-Compiler den ähnlichsten Code geben.

<div data-benchmark="mandelbrot"></div>

### `fasta`

Schreibt DNA als Text, einen zufälligen Buchstaben nach dem anderen, sechzig pro Zeile. TorbScripts `print` gibt jede
Zeile an das Betriebssystem, bevor es zurückkehrt; das C-Programm verlangt dasselbe mit `setlinebuf`, also machen
beide einen Systemaufruf pro Zeile. Dazwischen sammelt TorbScript die Buchstaben einer Zeile in einer Liste und macht
einen `String` daraus, wo das C jeden Buchstaben in den Puffer seiner Ausgabe legt.

<div data-benchmark="fasta"></div>

## Warum die VM langsamer ist

`torb run` ist dafür da, ein Programm sofort zu starten, während du es schreibst. Seine VM ist jung und selbst in
TorbScript geschrieben, und heute braucht sie ein Vielfaches der Zeit von Python. Am langsamsten ist sie, wo ein
Programm in seiner innersten Schleife ein Feld eines Records in einer Liste oder ein Element einer Liste ändert -
n-body und fannkuch-redux -, weil sie eine solche Stelle Schritt für Schritt über eine Beschreibung des Pfads erreicht.
[Der Entwurf der VM](../design/VM.md#10-open) nennt, was als Nächstes kommt. Ihre Zeit auf dieser Seite enthält das
Prüfen und Übersetzen des Programms, das die Zeile „Starten“ der Tabelle für sich misst.

## Alle Zahlen

<div data-benchmark="table"></div>

<div data-benchmark="machine"></div>

## Wie gemessen wird

- **Wo.** Auf dem Linux-Runner der Forge, nach jedem Nightly-Build, mit der Toolchain dieses Nightlys: jede Nacht
  dieselbe Maschine, und jede Sprache darauf im selben Lauf.
- **Was.** TorbScript mit `torb build` (sein Release-Profil, `-O2`), C mit `gcc -O2` - derselbe Compiler und dieselbe
  Optimierung - und die Python- und JavaScript-Programme des Benchmarks Game selbst mit `python3` und `node`. Jedes
  Programm benutzt einen Thread.
- **Geprüft.** Die Ausgabe jedes Programms wird Byte für Byte mit der des C-Programms verglichen, bevor irgendetwas
  gemessen wird.
- **Wie oft.** Jede Sprache läuft einmal zum Aufwärmen, dann fünfmal - die VM dreimal - abwechselnd, damit ein
  ausgelasteter Moment der Maschine alle trifft. Ein Diagramm zeigt den Median; die Tabelle zeigt auch den schnellsten
  und den langsamsten Lauf.
- **Zwei Eingaben.** Die VM und Python bräuchten für die Eingabe, die die Binaries in Sekunden schaffen, viele Minuten,
  also laufen sie eine kleinere, und C läuft sie auch: Jedes Verhältnis vergleicht mit C auf derselben Eingabe. Auf der
  kleinen Eingabe braucht C nur Millisekunden, deshalb sind diese Verhältnisse die gröberen.
- **Die VM** ist das `torb` des Nightlys, so wie man es herunterlädt: ein statisches Binary, gegen musl gebaut, dessen
  Speicherverwaltung langsamer ist als die der C-Bibliothek, die die anderen Programme benutzen - was vor allem
  binary-trees spürt.
- **Speicher.** Die Spitze des Speichers, den der Prozess hält, gemessen mit GNU `time`. Bei der VM ist der Prozess
  `torb` selbst, samt Compiler.

## Was nicht gemessen wird

Das sind kleine Programme, die je eine Sache tun, keine Anwendungen, und ein C-Compiler mit einem Satz Optionen.
Nichts hier benutzt mehr als einen Kern. Was TorbScripts Abstraktionen kosten, Muster für Muster, und was noch nirgends
gemessen wird, steht in [Performance](../PERFORMANCE.md#7-what-is-not-measured-here).

## Selbst messen

Jedes Programm und das Skript, das sie misst, liegen im Repository, in `benchmarks/game/`, mit der Lizenz der
Programme, die aus dem Benchmarks Game stammen. In einem Checkout mit gebautem `torb`:

```console
$ sh benchmarks/game.sh
$ sh benchmarks/game.sh --quick n-body
```

Das erste misst alles, wie die Forge, und schreibt `benchmarks/out/game/benchmarks.json`. Das zweite prüft nur, dass
ein Programm in jeder Sprache baut, läuft und dasselbe ausgibt. [Die Performance-Suite](../../benchmarks/README.md)
sagt mehr.
