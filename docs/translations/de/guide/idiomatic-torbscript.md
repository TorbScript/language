---
title: Idiomatisches TorbScript
summary: Die Gewohnheiten, mit denen Code wie die Standardbibliothek liest - Namen aus ganzen Wörtern, Werte vor geteilten Typen, eine geprüfte Tür für jede Regel, die ein Wert halten muss, using für Ressourcen und das Layout des Formatters.
kind: guide
status: stable
order: 130
prerequisites:
  - a-small-program.md
translates: 2ba65f7175c3
---

Code kann kompilieren und trotzdem nicht wie TorbScript lesen. Das hier sind die Gewohnheiten der Standardbibliothek,
die der Guide noch nicht gezeigt hat. Jede ist eine Regel und ein Beispiel; der Link danach hat die Details.

## Ziel

Am Ende dieser Seite liest dein Code wie die Standardbibliothek: die Namen, die Typen und die Ressourcen in der einen
Form, die die Sprache für sie gewählt hat.

## Namen sind ganze Wörter {#names}

**Schreib `Expression`, `squareRoot` und `Item`, nie `Expr`, `sqrt` oder `T`.** Ein Name ist `camelCase`, und nur ein
Typ, ein Trait oder ein Fall beginnt mit einem Großbuchstaben: `maxSize`, nie `MAX_SIZE`. Abkürzungen, die schon der
Name sind, bleiben: `Json`, `Http`, `min`, `max`.

```trb run
fn firstOrDefault<Item>(items: List<Item>, fallback: Item): Item {
  items.first() ?? fallback
}

print firstOrDefault([3, 4], 0)
// prints 3
```

Ein Trait mit einer Methode heißt wie die Methode, `Hash` oder `Show`: Nichts endet auf `-able`. Ein `Bool`-Feld ist
ein Adjektiv, `enabled`, nicht `isEnabled`; eine Methode darf fragen, `isEmpty()`. Siehe
[Naming](../language/syntax/naming.md).

## Ein Typ ist ein Wert, außer er braucht eine Identität {#values}

**Schreib standardmäßig `type`. Schreib `shared type` nur für ein Ding, das jeder, der es hält, als dasselbe sehen
muss: eine Verbindung, eine Datei, ein Fenster.**

```trb run
type Point {
  var x: Int
  var y: Int
}

var start = Point 0, 0
var moved = start
moved.x = 5
print "{start.x} {moved.x}"
// prints 0 5
```

Ein Wert wird nie aus Versehen geteilt, also passiert eine Änderung nur dort, wo sie steht. Siehe
[Shared types](../language/types/shared-types.md).

## Eine Regel, die ein Wert halten muss, hat eine Tür {#capsules}

**Muss jeder Wert eines Typs eine Regel halten, mach das Feld `private` und gib dem Typ eine `static fn`, die die Regel
prüft.** Das private Feld schließt den Konstruktor, also lässt sich die Prüfung nicht umgehen.

```trb run
type Percent {
  private value: Int

  static fn tryFrom(value: Int): Result<Percent, String> {
    if value < 0 || value > 100 {
      return Fail "{value} is not between 0 and 100"
    }
    Self value
  }

  fn percent(): Int {
    value
  }
}

print Percent.tryFrom(120)
print Percent.tryFrom(40).map({ _.percent() })
// prints Fail("120 is not between 0 and 100")
// prints Ok(40)
```

Die Referenz nennt so einen Typ eine Kapsel: [Data or capsule](../language/types/data-or-capsule.md).

## Der Erfolg ist der Wert {#wrapping}

**Eine Funktion, die eine `Option` oder ein `Result` zurückgibt, endet mit dem nackten Wert, nicht mit `Some(value)`
oder `Ok(value)`.** Schreib `None` und `Fail`; der Wert wird für dich eingepackt.

```trb run
fn find(names: List<String>, wanted: String): Int? {
  for index in 0..names.length() {
    if names[index] == wanted {
      return index
    }
  }
  None
}

print find(["ada", "alan"], "alan")
// prints Some(1)
```

`torb lint --rule redundant-wrap` findet den Rest, und `--fix` entfernt sie. Siehe
[Conversions](../language/types/conversions.md).

## Eine Ressource wird mit using gebunden {#resources}

**Eine Datei, ein Socket oder ein Lock ist ein `shared type` mit `Close`, gebunden mit `using`.** Niemand ruft `close()`
von Hand auf: Es läuft einmal, am Ende des Blocks, der die letzte Referenz hält.

```trb run
shared type Log with Close {
  name: String

  fn write(message: String) {
    print "{name}: {message}"
  }

  var fn close() {
    print "{name} closed"
  }
}

fn work() {
  using log = Log "audit"
  log.write "started"
}

work()
// prints audit: started
// prints audit closed
```

Siehe [Destructors](../language/execution/destructors.md).

## Der Formatter entscheidet das Layout {#format}

**Wo die Sprache zwei Schreibweisen erlaubt, wählt `torb format` eine.** Führe es vor dem Commit aus, und lass
`--check` einen Build scheitern, der nicht im Layout ist.

```console
$ torb format src
$ torb format --check src
```

Siehe [torb format](../tooling/torb-format.md).

## Weitere Gewohnheiten {#more-habits}

Jede davon ist eine Regel der Referenzseite, die sie verlinkt:

- Ein Feld-Standardwert ist eine Konstante; alles andere berechnest du in einer `static fn` -
  [Construction](../language/types/construction.md).
- Eine Closure, die ein `var` liest oder schreibt, geht an einen Aufruf, der sie ausführt, und wird nie gespeichert
  oder zurückgegeben - [Closures](../language/functions/closures.md).
- Ein Parameter, der eine Closure nimmt, sagt, wofür sie ist: `Predicate<Item>`, `Action<Item>`,
  `Transform<Item, Output>` - [Predicate, Action and Transform](../standard-library/function-types.md).
- `From` wird von Hand geschrieben, `Into` kommt damit, und es gibt keine Casts -
  [Conversions](../language/types/conversions.md).
- `await()` gibt den Wert zurück, und ein abgebrochener Task stoppt jeden, der auf ihn wartet -
  [Tasks](../language/concurrency-and-streams/tasks.md).
- Die Gewohnheiten aus Rust, Swift, Kotlin und TypeScript, die nicht kompilieren, mit der Meldung zu jeder -
  [What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md).

## Weiter

- [The language reference](../language/index.md) - die genaue Regel hinter jeder Gewohnheit hier.
- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - jede Form der Sprache auf einer Seite.
- [Why the language is like this](../explanation/index.md) - die Gründe hinter diesen Gewohnheiten.
