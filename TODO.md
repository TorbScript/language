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
  - **Erledigt:** Alle 25 reinen Durchreich-Closures in `compiler/`, `std/` und `examples/` sind auf die Namensform
    umgestellt (Funktionsnamen, `Type.function` und ein an `self` gebundenes `contains`); eine Closure in
    `examples/game-engine` bleibt, weil sie eine Methode auf dem Element aufruft.

- `String.join`, wieso als String static method und nicht als Methode auf iterables z.B.?
  - **Antwort:** Historisch - es gab keine Member mit eigener `where`-Klausel. Seit Spec-Lücke 9 gibt es sie
    (`fn toSet(self): Set<Item> where Item: Hash`), also geht es als Methode.
  - **Entschieden, wird gelöst:** `Iterable` bekommt `fn joined(separator: String = ""): String where Item: Show`
    (Partizip, weil es einen neuen Wert liefert). `String.join` fällt weg - ein Weg statt zwei; der Collector
    `joining(...)` bleibt für Pipelines mit Präfix/Suffix. Betrifft 65 Aufrufe, Stage 0 bekommt die Native dazu.
    Läuft in derselben Aufräumrunde.
  - **Erledigt:** `joined(separator:)` ist jetzt ein Member von `Iterable` in `std/prelude/src/iteration.trb`,
    `String.join` ist aus `std/prelude/src/string.trb` und aus der Stage-0-Native entfernt, und alle 65 Aufrufe in
    `compiler/`, `std/`, `examples/` und `bootstrap/tests/` sind auf `parts.joined(separator: ...)` umgestellt.

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

- `    assert(kinds("1..5") == [TokenKind.IntegerLiteral, TokenKind.DotDot, TokenKind.IntegerLiteral, TokenKind.EndOfFile])` hier könnte auch sein `    assert(kinds("1..5") == [.IntegerLiteral, .DotDot, .IntegerLiteral, .EndOfFile])` da der Typ durch den lhs-Ausdruck bereits bekannt ist oder?
  - **Antwort:** Ja, in der Sprache gilt das: `==` ist `Equals.equals(other: Self)`, die linke Seite legt `Self` fest,
    also ist `List<TokenKind>` der erwartete Typ der rechten Seite und damit jedes Elements (Checker 4.4 macht das).
    Dass es im Compiler ausgeschrieben steht, liegt an Stage 0: der untypisierte Interpreter löst `.Case` nur direkt
    neben `==` auf, nicht in einem Listen-Literal - geprüft: `kinds() == [.Number, .Dot]` vergleicht dort still
    ungleich.
  - **Wird gelöst:** Stage 0 bekommt die Auflösung elementweise gegen die andere Seite (kleine Änderung in
    `interpreter.rs`), danach werden die Tests auf die kurze Form umgestellt. Läuft in der Aufräumrunde.
  - **Erledigt:** `==`/`!=` lösen ein `.Case` jetzt auch elementweise auf, wenn es in einer Liste, einem Tupel oder
    einem `Some(...)` steckt (gegen die passende Stelle der anderen Seite); ein `.Case` ohne Gegenstück ist jetzt ein
    Laufzeitfehler statt eines stillen `false`. `compiler/tests/lexer.test.trb` ist die einzige Stelle im Repository
    mit genau diesem Muster (Liste von `TokenKind.X` neben `==` in `assert(...)`) und ist auf die kurze Form
    umgestellt - sowohl Stage 0 als auch der echte Checker akzeptieren sie dort trotz der `Expression<Bool>`-Zitierung
    von `assert`.

- Ich will, dass TorbScript potenziell auch zu PHP und JS kompiliert werden kann. PHP, damit es auf alten Webspaces läuft. JS, damit man Frontend-Gegenstücke für interaktive Apps in einer sprache halten kann. Muss nicht kompatibel in einer Form sein, wo JS/PHP das dann wieder sauber konsumieren kann (aber wenn das auch sinnvoll ginge, wieso nicht, dann soll es npm/TypeScript bzw. composer/PHP Pakete ausspucken)
  - **Antwort:** Passt in die Architektur: alles bis zur typisierten IR ist geteilt, ein Backend ist "IR → Text".
    C ist das erste, die Bytecode-VM das zweite; JS und PHP wären das dritte und vierte. Es gibt keine Destruktoren
    und keine beobachtbaren Releases (entschieden), also braucht ein Backend mit eigenem GC kein Reference Counting -
    nur Copy-on-Write für `var`-Pfade.
  - **Zurückgestellt bis nach dem Fixpunkt (neuer Meilenstein 9 in ARCHITECTURE.md).** Bis dahin halte ich die IR
    frei von C-Annahmen (Layouts/Nischen sind Hinweise, die ein Backend ignorieren darf). Drei Fragen, die du dann
    entscheiden musst, weil sie beobachtbar sind: (1) `Int` ist 64 Bit - in JS `BigInt` (langsam, exakt) oder
    `number` mit Bereichsprüfung auf 53 Bit (schnell, anderes Overflow-Verhalten)? PHP hat 64-Bit-Integer, passt.
    (2) `Task`/`Channel`: JS → Promises/Event-Loop geht sauber; PHP hat nichts Vergleichbares ohne Fibers/Extensions.
    (3) `std/fs`, `std/process` usw. existieren im Browser nicht - Capabilities pro Target, Compile error beim Import.
    Pakete ausspucken (npm mit `.d.ts`, composer mit PSR-4) ist machbar für `public`-APIs ohne Generics-Tricks;
    Werte-Semantik über die Grenze heißt: Kopie beim Rein- und Rausgehen.
  - Antworten vom User:
    1. number macht mehr Sinn auch für Integrationen
    2. Kann man mit PHP machen, recherchiere das. Notfalls eine eigene Event Loop dahinter z.B. mit stream_select oder den Features in der neuesten PHP Versionen. Amp/ReactPHP haben so was auch, nativ. via compose können auch durchaus Libraries erfordert werden, aber es sollte so kompatibel wie möglich zu klassischen PHP Webspaces sein, also man soll es im besten Falle schon reindroppen können einfach und es soll direkt funktionieren. (also z.B. libuv abfragen und im Notfall auf native impl zurückfallen, so kompatibel wie möglich auch zu älteren PHP Versionen noch)
    3. Da finde die beste Lösung, manches kann man vielleicht stubben oder simulieren sinnvoll (JS hat ja z.B. Web Filesystem API?), manches aber auch denke ich nicht
  - **Notiert, damit entschieden:** (1) JS: `Int` ist `number` mit Bereichsprüfung. (2) PHP: Tasks über eine Event
    Loop mit Erkennung zur Laufzeit (Fibers ab 8.1 → `ext-uv`/`ext-ev` wenn vorhanden → reines `stream_select`),
    Ziel "reinkopieren und es läuft" ohne Pflicht-Extensions; wie weit zurück die PHP-Version reichen kann, kläre ich
    in der Recherche zu Meilenstein 9 (ohne Fibers müssen Tasks als Zustandsmaschinen kompiliert werden - das macht
    das C-Backend ohnehin, also geht es). (3) Pro Target eine Capability-Tabelle: echte Implementierung, Simulation
    (`std/fs` im Browser über die File System Access API / OPFS, `std/environment` leer) oder Compile error beim
    Import (`std/process` im Browser).

- Die std soll eine saubere Dateisystem-Abstraktion bekommen, die mit Adaptern für z.B. S3, WebDav usw. erweitert werden kann (und ein paar gleich mit bringt)
- Die std soll net/socket, http, https, html, xml, yaml, toml, grpc, json, json-schema, json-patch und openapi unterstützen, in allen relevanten Versionen, als eigene Pakete
- Die std soll ein framework auf Spring-boot level bereitstellen unter anderem, also vollständige DI, Möglichkeit für hexagonale Architektur, MVC, ORM etc.
  - **Zu den drei std-Punkten (Dateisystem-Abstraktion mit Adaptern, Protokoll-/Format-Pakete, Framework):
    zurückgestellt bis nach dem Fixpunkt, als Meilenstein 10 "Breite der Standardbibliothek".** Vorher kann nichts
    davon laufen (Stage 0 lädt `std/` nicht, Netz/Tasks kommen mit Meilenstein 7). Reihenfolge, die ich vorschlage:
    1. `std/fs` als Trait `FileSystem` (lokal, In-Memory für Tests, dann S3/WebDAV als eigene Pakete) - das ist auch
       das Capability-Modell der Sandbox, also früh.
    2. `std/net` (Sockets) → `std/http` (Client/Server, TLS über die Plattform) → darauf `grpc`, `openapi`.
    3. Formate als reine TorbScript-Pakete auf `Encode`/`Decode`: `json` (existiert als Deklaration), `yaml`, `toml`,
       `xml`, `html`; `json-schema`, `json-patch`.
    4. Das Framework zuletzt. Wichtig für das Design: die Sprache hat bewusst keine Annotationen und keine
       Reflection - DI wird also **Compile-Zeit-Verdrahtung über Traits und Konstruktoren** (ein `Module`-DSL mit
       Receiver-Closures, wie `project.trb`), kein Classpath-Scanning. MVC-Routen und ORM-Mappings genauso über
       DSL + `Expression<Value>` (der Query-Provider im Beispiel ist der Keim des ORM). Das ist die eine Stelle, an
       der "Spring-Boot-Level" anders aussehen wird als Spring - sag, wenn du das anders willst.
  - Antworten vom User:
    - Ich will Springboot level und sauberkeit und flexibilität und umfang, aber passend zu TorbScript und dessen Funktionsweise. Springboot/Symfony entwickler
      sollten sich trotzdem schnell zurechtfinden können, auch wenn die Mechanismen anders implementiert sind.
  - **Notiert, das ist die Leitlinie für Meilenstein 10:** gleiche Begriffe und Schichten (Controller, Service,
    Repository, Konfiguration mit Profilen, Middleware/Filter, Events, Migrations, Test-Slices), gleicher Umfang -
    aber verdrahtet zur Compile-Zeit über Traits, Konstruktoren und ein Modul-DSL statt über Annotationen und
    Reflection. Vor dem Bau bekommst du einen Entwurf mit einem durchgängigen Beispiel (REST-Controller → Service →
    Repository → Datenbank), an dem du die Lesbarkeit für Spring-/Symfony-Leute beurteilen kannst.
  

- LSP für die repo-lokale VSCode extension
  - **Geplant als Meilenstein 8, nach dem Fixpunkt:** der Language Server ist der Compiler (`check` inkrementell pro
    Datei, die Tabellen des Checkers liefern Hover, Go-to-Definition, Completion, Diagnosen). Auf Stage 0 braucht
    ein `check` des Repos ~26 s, das ist als Server unbrauchbar - erst die native Binary macht es sinnvoll. Die
    Extension unter `.vscode/extensions/torbscript` bekommt dann den Client; bis dahin kann sie `torb check` als
    Task mit Problem-Matcher anbieten (klein, kommt in die Aufräumrunde).
  - **Erledigt:** `.vscode/tasks.json` hat jetzt "torb: check workspace" und "torb: test compiler"; die Extension
    liefert den `$torb`-Problem-Matcher für das zweizeilige Format von `compiler/src/cli/render.trb`
    (`error: <message>` dann ` --> <path>:<line>:<column>`). Abweichung von der Vorgabe: `fileLocation` ist
    `relative` (gegen `bootstrap/`) statt `absolute`, weil `torb check`/`parse` Pfade relativ zum Startverzeichnis
    ausgibt (`tree.display`, mit führendem `../` von `bootstrap/` aus) und nie absolute Pfade - mit `absolute` hätte
    VS Code sie an der falschen Stelle gesucht. Beide Tasks und der Matcher sind in der README der Extension
    dokumentiert.

- Überall sind multiline strings wie folgt formatiert:

    test "`for` over a range is a counter, a comparison and an increment, and `break` leaves it" {
    const source = r"""
fn count(limit: Int): Int {
  var total = 0
  for index in 0..limit {
    if index == 3 {
      continue
    }
    total = total + index
  }
  total
}
"""

aber ich will lieber so:

  test "`for` over a range is a counter, a comparison and an increment, and `break` leaves it" {
    const source = r"""
      fn count(limit: Int): Int {
        var total = 0
        for index in 0..limit {
          if index == 3 {
            continue
          }
          total = total + index
        }
        total
      }
    """

dabei sollte die Intentierung des ersten Zeichens der ersten Zeile mit Inhalt als Referenz für die gesamte Block-Intentierung dienen und dedentiert werden korrekt

fmt sollte das enforcieren auch eventuell 

  - **Entschieden, wird gelöst (nächste Runde, sobald die laufenden Zweige gemergt sind - es fasst fast jede
    Testdatei an):** `"""` und `r"""` rücken aus. Regeln:
    1. Steht nach dem öffnenden `"""` direkt ein Zeilenumbruch, gehört er nicht zum String.
    2. Die Einrückung der **ersten Zeile mit Inhalt** ist die Referenz; sie wird von jeder Zeile abgezogen. Eine
       Zeile mit Inhalt, die weniger eingerückt ist, ist ein Compile error ("This line is indented less than the
       first line of the string") - kein stilles Raten. Leerzeilen bleiben leer.
    3. Steht das schließende `"""` allein auf seiner Zeile, gehört seine Einrückung nicht zum String; der String
       endet dann mit dem Zeilenumbruch der letzten Inhaltszeile (wie heute).
    4. `"""text"""` auf einer Zeile bleibt, wie es ist. Interpolierte Werte werden nie ausgerückt, nur der Text.
    Beide Lexer (Rust und TorbScript) bekommen die Regel, danach werden alle mehrzeiligen Strings im Repo per
    Werkzeug umformatiert (Inhalt = Einrückung der öffnenden Zeile + 2). Einziger beobachtbarer Unterschied: der
    führende Zeilenumbruch fällt weg, Zeilennummern in erwarteten Diagnosen verschieben sich um eins - wird
    mit angepasst. `torb format` (Meilenstein 8) setzt die Form dann durch; steht dort auf der Liste.


- Ich weiß nicht, ob das bisherige "generische" Encode/Decode so cool ist und denke, es ist vielleicht besser, es gegen explizite JsonEncode/JsonDecode, TomlEncode/TomlDecode, XmlEncode/XmlDecode etc. auszutauschen, oder? Und vielleicht einen Mechanismus zu finden, wie Structs, die nur aus Feldern bestehen, die diese Traits haben, automatisch auch den Trait erben können oder so (Sodass z.B. User { id: Int, name: String } automatisch encodeable/decodable wäre?)
  - **Antwort - der zweite Teil existiert schon:** `Encode` und `Decode` werden für jeden `type` generiert, dessen
    Felder sie alle haben (wie `Show`, `Equals`, `Hash`; Checker 4.3, `derive.trb`). `User { id: Int, name: String }`
    ist also ohne eine Zeile Code encodebar - und zwar in **jedes** Format.
  - **Empfehlung zum ersten Teil: beim generischen Paar bleiben.** Es ist das serde-Modell: ein Typ beschreibt
    einmal seine *Struktur* (`Encode`), ein Format beschreibt einmal seine *Syntax* (`Encoder`: `JsonEncoder`,
    `TomlEncoder`, ...). Das sind N + M Implementierungen. Mit `JsonEncode`, `TomlEncode`, `XmlEncode` sind es N × M:
    jede Bibliothek müsste jedes Format kennen, ein neues Format (`std/yaml` von jemand anderem) könnte fremde Typen
    wegen der Kohärenzregel gar nicht nachrüsten, und `assert`/`describe`/`Expression`-Captures bräuchten ein eigenes
    viertes Trait. Dazu kommt: `List<Encode>` als Trait-Wert gibt es nur mit einem gemeinsamen Trait.
  - Was das generische Modell schlechter kann, ist formatspezifische Feinsteuerung (JSON-Feldname `user_id`, XML
    Attribut statt Element). Vorschlag dafür, ohne Annotationen: (a) Optionen am Encoder (`Json.encode(user,
    naming: .SnakeCase)`), (b) für Einzelfälle `extend User with Encode` von Hand (schlägt die generierte
    Implementierung), (c) Marker-Typen, wo das Format es braucht (`XmlAttribute<String>` als Feldtyp).
  - **Nachfrage im Chat ("geht XML sauber? MessagePack?"):** MessagePack ja, vollständig - das Datenmodell des
    `Encoder` (nothing/bool/int/unsigned/float/string/bytes, `sequence` und `map` mit `length: Int?`, `record`,
    `variant`) *ist* praktisch das von MessagePack/CBOR; das `length` gibt es genau für Binärformate mit
    Längenpräfix. XML nur zur Hälfte: **Daten-XML** (Konfiguration, RSS, SOAP-artig) geht per Konvention (record →
    Element, Felder → Kindelemente, Sequenz → wiederholte Elemente). **Dokument-XML** geht nicht sauber: Attribut
    gegen Element, Namespaces, Mixed Content (Text zwischen Elementen), signifikante Reihenfolge, Kommentare - das
    hat im generischen Modell keinen Platz (serde-XML-Crates behelfen sich mit Namens-Hacks wie `@id`/`$text`).
  - **Verfeinerter Vorschlag:** `Encode`/`Decode` bleibt die *Datenbindungs*-Schicht für alles, dessen Modell
    "Werte, Listen, Maps, Records" ist (JSON, MessagePack, CBOR, TOML, YAML, Query-Strings, DB-Zeilen). Formate mit
    eigenem Dokumentmodell bekommen **zusätzlich** eine eigene Schicht im eigenen Paket: `std/xml` mit `XmlNode`
    (Baum, wie `JsonValue`) und einem formatspezifischen Trait `XmlEncode`/`XmlDecode` für Typen, die volle
    Kontrolle brauchen; `Xml.encode(value: Encode)` deckt Daten-XML per Konvention ab, `Xml.write(value: XmlEncode)`
    den Rest. Gleiches Muster für HTML. Dein Instinkt stimmt also für XML/HTML - als Ergänzung, nicht als Ersatz.
  - **Entschieden (Chat, 2026-09-19): so wird es gemacht** - generisches `Encode`/`Decode` als Datenbindung,
    dokumentbasierte Formate (XML, HTML) bekommen zusätzlich ihre eigenen Encoder/Decoder-Traits im eigenen Paket.
    Steht im Konzept unter "Types, Values and Reflection" und im Decision Log.
  - ~~Deine Entscheidung:~~ bleibt es bei `Encode`/`Decode` + (a)-(c)? Dann ist nichts zu tun außer (a)-(c) bei den
    Formatpaketen (Meilenstein 10) mitzuplanen. Wenn du trotzdem formatspezifische Traits willst, sag es - dann
    würde ich sie *zusätzlich* als optionale Überschreibung vorschlagen, nicht als Ersatz.

- Die native-methoden sollten so gering wie möglich gehalten werden, um die Portabilität und Wartbarkeit des Codes zu gewährleisten. Lass uns drauf achten,
  dass sie leicht in alle Sprachen zu übersetzen sind. Eventuell gibt es ein native {} konstrukt in dem man direkt IR schreiben kann mit interpolation? das würde die Oberfläche und den ganzen stringly typed code im compiler besser machen und hilft eventuell auch wenn man Richtung SIMD/Grafikprogrammierung geht. Die IR könnte auch sauber lexed/parsed/gecheckt werden und highlighting haben
- Vielleicht kann man beim Übersetzen in z.B. C mit Template-Files irgendwie arbeiten, damit es nicht so stringly im Code ist einfach?
  - **Antwort zu "Natives klein halten":** Einverstanden, und es wird mit JS/PHP zwingend: heute sind es 363
    Manifest-Einträge (116 Intrinsics = IR-Operationen, 123 C-Runtime-Funktionen, 21 abgeleitet, Rest geplant) - die
    123 müsste jedes Backend nachbauen. Vorschlag, drei Schichten:
    1. **Kernel:** eine kleine, feste Menge IR-Intrinsics (Ganzzahl-/Float-Arithmetik, Vergleiche, ein roher
       `Buffer<Item>` mit get/set/grow, Bytes ↔ Zahl), die *jedes* Backend können muss. Ziel: zweistellig.
    2. **Natives mit Fallback-Body:** `native fn trim(self): String { ...TorbScript... }` - ein Backend *darf* eine
       eigene Implementierung haben (C-Runtime, JS-`String.prototype.trim`), sonst wird der Body kompiliert. Damit
       ist `String`, `List`, `Map`, UTF-8, Float-Formatierung usw. portables TorbScript auf dem Kernel, und die
       C-Runtime ist nur noch Beschleunigung. Neue Backends starten mit dem Kernel und sind sofort vollständig.
    3. **Plattform-Capabilities** (Dateien, Netz, Uhr, Prozess): pro Target über die Capability-Tabelle (siehe
       JS/PHP oben) - die sind naturgemäß nativ.
  - **`native { ... }` mit IR:** gefällt mir als Bindung der Kernel-Schicht - dann steht in `std/` selbst, welche
    IR-Operation `Int64.add` ist, statt in einer Namens-Tabelle im Compiler (`backend/c/natives.trb`), und der
    "stringly" Teil des Manifests verschwindet. Die IR hat schon ein Textformat und einen Verifier; es fehlen ein
    Parser dafür, die Typprüfung der Slots gegen die TorbScript-Parameter und das Highlighting. Für SIMD/GPU wäre
    es derselbe Mechanismus mit weiteren Intrinsics.
  - **Zurückgestellt bis direkt nach dem Fixpunkt (vor Meilenstein 9), bewusst:** bis 5.14 ändert sich das
    IR-Format noch mit jedem Teilschritt, und der Fixpunkt braucht die vorhandene C-Runtime. Ab sofort gilt aber
    die Regel für alle Backend-Agents: **keine neue C-Native, wenn es in TorbScript auf vorhandenen Natives geht**
    (steht in CONTRIBUTING.md). Nach dem Fixpunkt: Audit des Manifests → Kernel festlegen → Fallback-Bodies →
    `native`-IR-Blöcke. Geschmacksfrage an dich, wenn es so weit ist: Syntax (`native fn add(...) = ir { ... }`
    gegen einen Block im Body).
    Antwort User: Wenn die ganze Funktion IR ist, dann einfach "native fn add(...): X { /** hier einfach IR, durch "native" modifier */ }", ansonsten
    native {} konstrukt. native könnte ggf. auch unstable sein oder so? Ist das wie in Rust unsafe etwas?

  - **Antwort zu "Templates statt stringly C":** Ja für die *festen* Teile (Kopf der Übersetzungseinheit, `main`,
    die Retain/Drop-Helfer je Layout): sobald die ausgerückten `"""`-Strings mit Interpolation gemergt sind (läuft),
    sind das lesbare C-Blöcke direkt im Emitter - eingebettete Template-Dateien bräuchten ein Compile-Zeit-`embed`,
    das wäre Meta-Programmierung durch die Hintertür. Für die *Bodies* (Instruktion für Instruktion) ist ein kleiner
    strukturierter C-Writer (Blöcke, Einrückung, Ausdrücke als Werte statt Strings) das richtige Werkzeug, keine
    Templates. **Wird gelöst** als Refactoring des Emitters nach 5.5 und dem Dedent-Merge, vor 5.6.

- Jetzt gerade wird sehr viel mit Funktionen und expliziten parametern gearbeitet statt mit methoden und UFCS, woran liegt das?