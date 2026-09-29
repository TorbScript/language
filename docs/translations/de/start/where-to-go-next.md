---
title: Wie es weitergeht
summary: Installiere TorbScript, um Programme auf deinem eigenen Computer laufen zu lassen, und entscheide, was als Nächstes kommt - der Guide, die Referenz oder ein eigenes Projekt.
kind: lesson
status: stable
order: 13
translates: 4b9308030f99
---

Du hast Programme mit Namen, Entscheidungen, Schleifen, Listen, Funktionen, Typen, Fällen und Antworten geschrieben,
die fehlen oder scheitern können. Das ist der Kern des Programmierens, in jeder Sprache.

Um Programme auf deinem eigenen Computer laufen zu lassen, installiere TorbScript. Der Befehl wird in ein Terminal
eingegeben: das Fenster, in dem du einem Computer Befehle als Text gibst. Unter Linux und macOS ist es der Befehl
unten; unter Windows, in PowerShell, ist es `irm https://torb.dev/install.ps1 | iex`.

```console
$ curl -fsSL https://torb.dev/install.sh | sh
```

## Ein Programm auf deinem Computer ausführen

Speichere ein Programm in einer Datei, deren Name auf `.trb` endet, und führe sie mit `torb run` und dem Dateinamen
aus. Was das Programm ausgibt, erscheint unter dem Befehl.

```console
$ torb run hello.trb
Hello from my computer!
```

## Was als Nächstes zu lesen ist

- **Der [Guide](../guide/index.md)** erklärt die Sprache tiefer, für Menschen, die programmieren können – wozu jetzt
  auch du gehörst.
- **Die [Referenz](../index.md)** hat jede Regel der Sprache und jeden Teil ihrer Standardbibliothek, für wenn du
  genau wissen willst, wie etwas funktioniert.
- **Ein eigenes Projekt** lehrt mehr als jede Seite: eine Liste deiner Bücher, ein Quiz, ein kleines Spiel.

## Übung

Installiere TorbScript, speichere dieses Programm als `hello.trb`, ändere es so, dass es `Hello from my computer!`
ausgibt, und führe es mit `torb run hello.trb` aus. Du kannst die Änderung erst hier ausprobieren.

```trb exercise
print "Hello from the browser!"
```

<details>
<summary>Tipp</summary>

Nur der Text zwischen den Anführungszeichen ändert sich. Wird `torb` nach der Installation nicht gefunden, öffne ein
neues Terminal-Fenster.

</details>

<details>
<summary>Lösung</summary>

```trb run
print "Hello from my computer!"
// prints Hello from my computer!
```

</details>

## Zusammenfassung

`torb run file.trb` führt ein Programm auf deinem Computer aus, und der Guide ist der nächste Schritt.
