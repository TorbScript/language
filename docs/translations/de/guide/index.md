---
title: TorbScript lernen
summary: Für alle, die schon programmieren - eine Tour von fünfzehn Minuten, dann eine kurze Seite pro Idee, alles an einem Abend geschafft.
kind: index
status: stable
order: 10
translates: 7371805ab4c8
---

Fang mit [der Tour](tour.md) an: fünfzehn Minuten, und jedes Beispiel läuft auf der Seite. Dann
[installiere TorbScript](installing-and-running.md) und geh die Seiten unten der Reihe nach durch. Jede sagt, was du
danach kannst, zeigt ein Beispiel und nennt die nächste Seite. Zusammen dauern sie einen Abend.

Kennst du Rust, TypeScript, Python, Go, Kotlin oder Swift, dann legt
[Coming from another language](coming-from/index.md) in einer Tabelle das, was du kennst, auf TorbScript um.

## Für wen das ist

Für alle, die schon in einer anderen Sprache programmieren können. Hast du noch nie programmiert, fängt der
[Kurs](../start/index.md) bei null an. Willst du die genaue Regel hinter etwas, hat die
[Sprachreferenz](../language/index.md) eine Seite pro Konstrukt.

<!-- torb:index:begin -->

## Abschnitte

- **[Coming from another language](coming-from/index.md)** – Eine Seite pro Sprache, die du vielleicht kennst - was direkt passt, die Gewohnheiten, über die du stolperst, und was TorbScript weglässt.

## Seiten

- **[Eine Tour durch TorbScript](tour.md)** – Die ganze Sprache in fünfzehn Minuten für alle, die schon programmieren - Bindungen, Aufrufe, Funktionen, Typen, Fälle, Fehler, Traits und Pipelines, je ein kurzes Beispiel.
- **[Installieren und ausführen](installing-and-running.md)** – TorbScript mit einem Befehl installieren, eine Datei ausführen und ein Projekt mit einem Test anlegen - die fünf Befehle, die du jeden Tag brauchst.
- **[Werte und Bindungen](values-and-bindings.md)** – Eine Bindung ist const oder var, ein zweiter Name ist immer eine Kopie, und eine Änderung geht über den Pfad, an dem der Wert liegt.
- **[Funktionen und Closures](functions-and-closures.md)** – Eine Funktion mit typisierten Parametern, Standardwerten und Labels deklarieren, eine Closure schreiben und sie als letztes Argument eines Aufrufs übergeben.
- **[Typen und Methoden](types-and-methods.md)** – Einen Typ mit Feldern deklarieren, ihm Methoden geben und eine Methode, die den Wert ändert, von einer unterscheiden, die eine geänderte Kopie zurückgibt.
- **[Fälle und Pattern Matching](cases-and-matching.md)** – Einen Typ deklarieren, dessen Wert einer von mehreren Fällen ist, und ihn mit einem match auseinandernehmen, das jeden Fall behandeln muss.
- **[Traits](traits.md)** – Als Trait deklarieren, was ein Typ kann, ihn einem Typ jetzt oder später geben und den Trait als Typ benutzen, der jeden davon hält.
- **[Fehler](errors.md)** – Eine Funktion, die scheitern kann, gibt ein Result zurück, ein Wert, der fehlen kann, ist eine Option, und der Aufrufer behandelt beides mit match, dem Fragezeichen oder einem Ersatzwert.
- **[Kollektionen und Pipelines](collections-and-pipelines.md)** – Eine Liste, eine Map und eine Menge bauen, eine an Ort und Stelle ändern oder eine geänderte Kopie bekommen, und Werte durch eine Pipeline aus Schritten schicken.
- **[Kontrollfluss und eigene Konstrukte](control-flow-and-dsls.md)** – Ifs und Schleifen funktionieren wie erwartet, und eine neue Kontrollstruktur oder ein Konfigurationsblock ist eine gewöhnliche Funktion, die du selbst schreiben kannst.
- **[Module und Pakete](modules-and-packages.md)** – Ein Programm mit public und use auf Dateien aufteilen, aus der Standardbibliothek importieren und ein Projekt so anlegen, dass seine Tests seinen Code erreichen.
- **[Alles zusammensetzen](a-small-program.md)** – Ein kleines Programm - ein Typ mit Fällen, ein Typ mit Feldern, eine Funktion, die scheitern kann, und eine Pipeline -, das benutzt, was der Guide gezeigt hat.
- **[Idiomatisches TorbScript](idiomatic-torbscript.md)** – Die Gewohnheiten, mit denen Code wie die Standardbibliothek liest - Namen aus ganzen Wörtern, Werte vor geteilten Typen, eine geprüfte Tür für jede Regel, die ein Wert halten muss, using für Ressourcen und das Layout des Formatters.

<!-- torb:index:end -->
