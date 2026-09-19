# TODOs oder offene Fragen, du kannst sie beantworten, zurückstellen oder lösen

- `y { x(_) }` wird oft verwendet, aber ist das nicht dasselbe wie `y(x)`?
  - **Antwort:** Ja. Eine Funktion ist ein Wert ("a method is a constant that holds a closure"), also ist
    `items.map(stripMargin)` dasselbe wie `items.map { stripMargin(_) }` - auf Stage 0 geprüft, beides läuft.
    Die Closure braucht man nur, wenn sie mehr tut als durchreichen: Methode auf dem Element (`{ _.trim() }`),
    weitere Argumente (`{ parse(_, strict: true) }`), oder wenn der Parameter ein `Expression<...>` ist (dann wird
    der Ausdruck zitiert, und `{ x(_) }` ist ein anderer Baum als der Name `x`).
  - **Wird gelöst:** Die ~20 reinen Durchreich-Closures in `compiler/`, `std/` und `examples/` werden auf die
    Namensform umgestellt, und der Linter (Meilenstein 8) bekommt dafür eine Regel. Steht in der Aufräumrunde nach
    den laufenden Checker-Merges.

- `String.join`, wieso als String static method und nicht als Methode auf iterables z.B.?
  - **Antwort:** Historisch - es gab keine Member mit eigener `where`-Klausel. Seit Spec-Lücke 9 gibt es sie
    (`fn toSet(self): Set<Item> where Item: Hash`), also geht es als Methode.
  - **Entschieden, wird gelöst:** `Iterable` bekommt `fn joined(separator: String = ""): String where Item: Show`
    (Partizip, weil es einen neuen Wert liefert). `String.join` fällt weg - ein Weg statt zwei; der Collector
    `joining(...)` bleibt für Pipelines mit Präfix/Suffix. Betrifft 65 Aufrufe, Stage 0 bekommt die Native dazu.
    Läuft in derselben Aufräumrunde.

- std soll ein Modul analog zu Tools wie Clap bekommen (Einfache Multi-command CLIs)
  - **Zurückgestellt bis nach dem Fixpunkt (Meilenstein 6), dann als `std/cli`.** Der erste Nutzer ist der Compiler
    selbst (`compiler/src/main.trb` parst seine Argumente heute per `match` auf Listen). Die API soll die DSL-Mittel
    der Sprache nutzen statt Annotationen - Skizze, über die du entscheiden solltest, bevor sie gebaut wird:

    ```trb
    const cli = command("torb") {
      description "The TorbScript toolchain"
      command("check") {
        description "Check projects, files or a whole workspace"
        flag "statistics", help: "Print typed and deferred expressions per module"
        arguments "paths", help: "Projects, directories or files"
        run { input => check(input.arguments("paths"), statistics: input.flag("statistics")) }
      }
    }
    cli.run(Process.arguments())
    ```

    Offene Geschmacksfragen: typisierte Optionen (`option<Int>("jobs", default: 4)`) gegen einen Decode in einen
    eigenen Typ (`input.decode<CheckOptions>()`, nutzt `Decode` wie `std/json`); Hilfe-/Fehlertexte automatisch;
    Shell-Completion später.
