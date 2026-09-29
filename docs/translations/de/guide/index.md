---
title: TorbScript lernen
summary: Ein Guide für Programmierer, die schon eine andere Sprache kennen - eine 15-minütige Tour, dann eine Idee pro Seite, an einem Abend zu schaffen.
kind: index
status: stable
order: 10
translates: de483409bf00
---

Wenn du schon beruflich Code schreibst, starte mit [TorbScript in 15 Minuten](torbscript-in-15-minutes.md) oder
deiner Sprache in [Coming from another language](coming-from/index.md) – beides bringt dich noch heute dazu, echte
Programme zu schreiben. Die nummerierten Seiten unten gehen denselben Weg noch einmal, langsamer und eine Idee pro
Seite, für wenn fünfzehn Minuten an einer Stelle nicht gereicht haben. Jede hat ein `## Ziel`, das sagt, was an ihrem
Ende funktioniert, und endet mit der nächsten Seite. Willst du die genaue Regel hinter etwas statt eines
Durchlaufs, geh direkt zu [der Sprachreferenz](../language/index.md).

Die zwölf Dateien von [`examples/tour`](../../examples/tour) sind derselbe Stoff als lauffähiger Code; der Pfad unten
verlinkt dorthin, wo es hilft.

## Wofür das hier ist

Lernmaterial für jemanden, der noch nie TorbScript geschrieben hat: eine schnelle Tour, eine Tabelle pro Sprache, aus
der Leute ankommen, und ein langsamerer Pfad mit einer Seite pro Idee, in Reihenfolge. Eine Seite hier setzt
Programmiererfahrung voraus, keine TorbScript-Erfahrung, und zeigt, was du tippst und was herauskommt. Die
Verantwortung für das Ergebnis liegt bei der Seite, nicht beim Leser.

Was hier nicht hingehört: die genauen Regeln eines Konstrukts, die in [`language/`](../language/index.md) stehen;
eine Aufgabe für sich allein, die ein [How-to](../how-to/index.md) ist; und das Argument für eine Design-Entscheidung,
die eine [Explanation](../explanation/index.md) ist. Eine Seite darf eine Regel in einem Satz nennen und die Seite
verlinken, die sie vollständig hat.

<!-- torb:index:begin -->

## Abschnitte

- **[Coming from another language](coming-from/index.md)** – Eine Seite pro Sprache – eine Tabelle der 10 bis 15 Dinge, die direkt übertragbar sind, der 5, die dich überraschen werden, und was absichtlich fehlt.

## Seiten

- **[Die Sprache in sechzig Sekunden](the-language-in-sixty-seconds.md)** – Das mentale Modell von TorbScript auf einem Bildschirm: Werte, Bindungen, kein null, keine Exceptions, Traits, und Aufrufe, die als Commands geschrieben werden.
- **[TorbScript in 15 Minuten](torbscript-in-15-minutes.md)** – Die schnellste ehrliche Tour durch TorbScript für einen arbeitenden Programmierer, ein kurzes Beispiel und ein paar Sätze pro Idee.
- **[Dein erstes Programm ausführen](installing-and-running.md)** – Die Toolchain bauen, eine einzelne Datei ausführen, und ein Projekt mit Manifest, Quelldatei und Test erstellen.
- **[Werte und Bindungen](values-and-bindings.md)** – Warum const und var die ganze Mutationsgeschichte sind, was eine Kopie kostet, und die eine Falle, in die jeder tappt, der von einer Sprache mit Referenzen kommt.
- **[Funktionen und Closures](functions-and-closures.md)** – Wie du eine Funktion deklarierst, wann sie ihren Rückgabetyp ausschreiben muss, und die eine Closure-Form, die die Sprache hat.
- **[Typen und Methoden](types-and-methods.md)** – Wie du einen Typ deklarierst, ihm Methoden hinzufügst, und ein Verb, das ihn ändert, von dem Partizip unterscheidest, das eine Kopie zurückgibt.
- **[Fälle und Pattern Matching](cases-and-matching.md)** – Wie du einen Typ mit mehr als einer Form deklarierst und ihn mit einem match auseinandernimmst, das jeden Fall abdecken muss.
- **[Traits](traits.md)** – Wie du eine Fähigkeit deklarierst, sie einem Typ gibst, und den Trait selbst als Typ benutzt, der verbirgt, welcher konkrete Typ dahintersteht.
- **[Fehler](errors.md)** – Wie eine Funktion mit Result sagt, dass sie scheitern kann, und wie ein Aufrufer das mit match oder dem Fragezeichen-Operator behandelt.
- **[Kollektionen und Pipelines](collections-and-pipelines.md)** – Wie du eine List, Map und Set baust, eine an Ort und Stelle änderst oder eine geänderte Kopie bekommst, und Werte durch eine faule Pipeline ziehst.
- **[Kontrollfluss und eigene Konstrukte](control-flow-and-dsls.md)** – if, for, while und loop wie erwartet, und warum unless eine gewöhnliche Funktion ist, die du selbst hättest schreiben können.
- **[Module und Pakete](modules-and-packages.md)** – Wie use einen Namen aus einer anderen Datei oder der Standardbibliothek hereinholt, und was public für eine Deklaration auf oberster Ebene bedeutet.
- **[Tests und die Toolchain](tests-and-tooling.md)** – Wie du mit test, group und assert einen Test schreibst, und die zwei Befehle, die prüfen, ob das Geschriebene korrekt ist.
- **[Alles zusammensetzen](a-small-program.md)** – Ein kleines Programm - ein Typ mit Fällen, eine Funktion, die scheitern kann, und eine Pipeline -, das alles benutzt, was dieser Pfad gelehrt hat.
- **[Idiomatisches TorbScript](idiomatic-torbscript.md)** – Die Gewohnheiten, die TorbScript wie TorbScript lesen lassen - Namen, Mutation, Aufrufe, Typen, Fehler, Closures, Ressourcen und Tasks - je als eine Regel, ein lauffähiges Beispiel und der Grund dahinter.

<!-- torb:index:end -->
