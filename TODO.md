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
  - **Erledigt (Lexer-Regel):** beide Lexer dedentieren `"""`/`r"""` jetzt nach den vier Regeln, mit Tests in beiden
    Testsuiten und den verschobenen Zeilennummern in den betroffenen Erwartungen nachgezogen; das Umformatieren der
    bestehenden Strings folgt als eigener Schritt.


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
  - **Notiert, damit entschieden (Syntax):** `native fn add(self, other: Int64): Int64 { <IR> }` - der `native`-
    Modifier macht den Body zu IR; `native { <IR> }` als Block-Ausdruck mitten in einer normalen Funktion. Eine
    `native fn` ohne Body bleibt "das liefert die Plattform" (Capabilities). Folge für meinen Schichten-Vorschlag:
    der "Fallback-Body" braucht dann **keine** Syntax - jede gewöhnliche `fn` in `std/` darf von einem Backend
    beschleunigt werden (das ist Privatwissen des Backends, eine Tabelle dort), `native` heißt immer IR.
  - **Ist das wie `unsafe`?** In der Rolle ja: die eine Stelle, an der die Garantien der Sprache nicht vom
    Typchecker kommen. IR kann Werte-Semantik und Ownership verletzen; der IR-Verifier prüft Form, Slot-Typen und
    die Ownership-Invarianten (jeder Wert genau einmal konsumiert/freigegeben), aber nicht den Sinn. Unterschied zu
    Rust: es ist kein Block, den jeder überall schreiben darf. Vorschlag: `native` bleibt **`std/*` vorbehalten**
    (gilt heute schon, Spec-Lücke 22), bis das IR-Format versioniert und stabil ist - sonst bricht jedes
    Fremdpaket mit jedem Compiler-Update ("unstable" ist genau das). Danach als Capability wie `foreign`:
    sichtbar in `project.trb`, `torb add` zeigt sie an, das Lock-File merkt sie sich. Das deckt dein "unstable" ab,
    ohne ein zweites Schlüsselwort.

  - **Antwort zu "Templates statt stringly C":** Ja für die *festen* Teile (Kopf der Übersetzungseinheit, `main`,
    die Retain/Drop-Helfer je Layout): sobald die ausgerückten `"""`-Strings mit Interpolation gemergt sind (läuft),
    sind das lesbare C-Blöcke direkt im Emitter - eingebettete Template-Dateien bräuchten ein Compile-Zeit-`embed`,
    das wäre Meta-Programmierung durch die Hintertür. Für die *Bodies* (Instruktion für Instruktion) ist ein kleiner
    strukturierter C-Writer (Blöcke, Einrückung, Ausdrücke als Werte statt Strings) das richtige Werkzeug, keine
    Templates. **Wird gelöst** als Refactoring des Emitters nach 5.5 und dem Dedent-Merge, vor 5.6.
  - **Erledigt:** `backend/c/writer.trb` macht Ausdrücke, Anweisungen und Deklarationen zu Werten mit einem Renderer, der Klammern, Einrückung und Leerzeilen an je einer Stelle entscheidet, und Kopf und `main` sind ausgerückte `"""`-Blöcke - das erzeugte C ist dabei byteweise unverändert (siehe docs/BACKEND.md, "How the C emitter is written").

- Jetzt gerade wird sehr viel mit Funktionen und expliziten parametern gearbeitet statt mit methoden und UFCS, woran liegt das?  - **Antwort:** Drei Gründe, zwei davon sind inzwischen weg.
    1. Als Parser und Checker entstanden, war nicht entschieden, ob ein `extend` eines eigenen Typs in einer anderen
       Datei "Teil des Typs" ist. Ein `Parser` mit sechzig Methoden in fünf Dateien hätte diese Regel gebraucht,
       Funktionen `parseExpression(var parser: Parser)` brauchten sie nicht (steht so in ARCHITECTURE.md). Die Regel
       **gibt es jetzt** (`extend` im selben Paket gehört zum Typ) - der Grund ist weg.
    2. Stage 0: eine `var self`-Methode, die ein Feld von `self` an eine andere `var self`-Methode gibt, liest dort
       einen ausgeräumten Platz (Falle 2 in CONTRIBUTING.md). Mit freien Funktionen tritt das seltener auf. Der Grund
       fällt mit dem Fixpunkt weg.
    3. Zyklische Importe per Namen (`use parsePattern from "./patterns"`) waren von Anfang an erlaubt; Methoden in
       `extend`-Blöcken über mehrere Dateien sieht man erst, seit die Sichtbarkeitsregel steht.
    Zahlen heute in `compiler/src`: 1471 freie Funktionen, 261 Methoden, kein einziges `extend`.
  - **UFCS gibt es in TorbScript nicht**, und ich würde es nicht einführen: `value.f(x)` → `f(value, x)` macht aus
    jedem Funktionsnamen einen möglichen Member, kollidiert mit "ein Namensraum für Member" und mit der
    Sichtbarkeitsregel für Extensions, und es wäre ein zweiter Weg neben `extend`. Wer `parser.parseExpression()`
    schreiben will, schreibt `extend Parser { fn parseExpression(var self): Expression { ... } }` - explizit,
    importierbar, im selben Paket Teil des Typs.
  - **Vorschlag - deine Entscheidung (Geschmack):** Nach dem Fixpunkt stelle ich den Compiler dort auf Methoden um,
    wo der erste Parameter eindeutig der Empfänger ist (`var parser: Parser`, `var checker: Checker`,
    `var lowering: Lowering`, `var builder: FunctionBuilder`, `program: IrProgram`): `extend Parser { ... }` je
    Datei, Aufrufe werden `parser.expression()` statt `parseExpression(parser)`. Vorher nicht: es sind ~1000
    Funktionen und alle Aufrufer, und erst der native Compiler prüft so einen Umbau in Sekunden statt in einer
    Stunde. Reine Helfer ohne Empfänger (`editDistance(a, b)`, `normalizePath(path)`) bleiben Funktionen. Wenn du
    bei "Funktionen zuerst" bleiben willst, ist das auch konsistent - sag, was dir lieber ist.
  - **Entschieden (Chat, 2026-09-19: "Ja mach das alles so"):** kein UFCS (steht im Decision Log); `native` wie oben
    (Body = IR, `std/*` vorbehalten bis das IR-Format stabil ist, danach Capability; steht im Konzept unter "Foreign
    Functions"); nach dem Fixpunkt stelle ich den Compiler auf `extend Parser { ... }`/`extend Checker { ... }` um,
    wo der erste Parameter der Empfänger ist. Eingeplant direkt nach Meilenstein 6.

- `fn printSorted<Item>(items: List<Item>) where Item: Compare + Show`, ich fände `where Item: Compare & Show` besser, es ist klarer irgendwie finde ich. Wieso nicht &?
  - **Antwort:** `+` kam von Rust, ein eigener Grund steckt nicht dahinter. `&` ist besser: es ist das Gegenstück zum
    `|` der Literal-Typen (`"online" | "offline"` = eins davon, `Compare & Show` = beides), `+` ist sonst `Add`, und
    `&` ist frei, weil die Sprache keine Bit-Operatoren hat (nur `&&`).
  - **Entschieden, wird gelöst:** `Compare & Show` überall (Typpositionen, Bounds, `where`). Beide Lexer/Parser, die
    19 Stellen in `std/`, `examples/`, `compiler/`, Konzept, VS-Code-Grammatik. `+` in einer Typposition wird ein
    Parse-Fehler mit Hinweis auf `&`. Läuft als eigener kleiner Agent.
  - **Erledigt:** `&` ist jetzt der Token für eine Schnittmenge von Traits, in beiden Lexern/Parsern (`+` in
    Typposition, Bound oder `where`-Klausel ist ein Parse-Fehler mit Hinweis auf `&` und wird als `&` weiterverarbeitet),
    in den ca. 19 Fundstellen in `std/`, `examples/`, `compiler/`, den Tests, der VS-Code-Grammatik und im Konzept.
- Hier:

```
type Meters with Add, Subtract, Compare by value {
  value: Float
}

type Seconds with Add, Subtract, Compare by value {
  value: Float
}
```

finde ich die `by value` Syntax irgendwie doof, weil sie direkt Fragen auf lässt:
  1. Kann ich auch `Add by valueA, Subtract by valueB` schreiben? Was genau passiert dann?
  2. Was wenn ich manche nicht by `value` machen möchte? z.B., with `Add by valueA, Subtract, Compare`, es ist sofort unklar, was auf was geht irgendwie.

Wie könnte man die Syntax verbessern? Und müsste das durch das Derive-System wie bei Encode/Decode, Show etc. eh nicht auch automatisch gehen?

```
type Seconds {
  value: Float // Weil Float Add, Subtract, Compare hat und es Single-Value ist, erbt Seconds das alles automatisch auch
}

var secs = Seconds(5) * Seconds(2)
```

Falls wir das hier _nicht_ automatisch deriven, sollten wir vielleicht ein implizietes Konstrukt für Deriven finden? Rust hat eines, aber wir wollen kein Metaprogramming. Ich weiß es nicht, schlag du was vor.
  - **Antwort zu "automatisch ableiten":** Bewusst nicht. Abgeleitet wird, was für *jeden* Typ strukturell dasselbe
    bedeutet: `Show`, `Equals`, `Hash`, `Encode`, `Decode`, `copy`. Arithmetik und Ordnung sind eine Aussage über die
    *Bedeutung*: `Seconds(5) * Seconds(2)` wären Quadratsekunden, `Meters * Meters` eine Fläche, eine `EntityId` darf
    man gar nicht addieren. Genau dafür gibt es eigene Typen statt `Float` - würde ein Ein-Feld-Typ alles erben, wäre
    er nur noch ein Alias mit anderem Namen. Also bleibt es eine Auswahl; die Frage ist nur die Schreibweise.
  - **Zu deinen zwei Fragen:** heute bindet `by` an die ganze `with`-Liste (ein Feld für alle), `Add by a, Subtract
    by b` geht nicht, und "manche delegiert, manche von Hand" auch nicht - beides ist aus der Zeile nicht ablesbar.
    Du hast recht, das ist die Schwäche.
  - **Vorschlag - die Delegation gehört ans Feld, nicht an den Typ:**

    ```trb
    type Meters {
      value: Float provides Add, Subtract, Compare
    }

    type Money with Show {
      amount: Decimal provides Add, Subtract, Compare
      currency: Currency provides Equals
    }
    ```

    Liest sich als "dieses Feld liefert dem Typ diese Traits". Damit ist alles aus der Zeile ablesbar: mehrere Felder
    können Verschiedenes liefern; was in `with` steht, aber von keinem Feld geliefert wird, schreibt man von Hand
    oder wird abgeleitet; zwei Felder, die denselben Trait liefern, sind ein Fehler. Die Regeln bleiben die von
    heute: weitergereicht werden die *geforderten* Member, Default-Member kommen vom Trait (Lücke 14); ein Ergebnis
    vom Typ des Feldes wird wieder eingepackt (`Meters + Meters` ist `Meters`), was nur geht, wenn sich der Typ aus
    dem einen Feld bauen lässt (die übrigen Felder haben Defaults oder es gibt keine) - bei `Money` wäre `Add` also
    ein Fehler mit genau dieser Begründung, `Compare` und `Equals` gehen. Das ist das "implizite Konstrukt": ein
    Wort, keine Annotation, kein Makro.
  - **Deine Entscheidung (Geschmack):** das Wort. `provides` ist mein Favorit; Alternativen `value: Float as Add,
    Subtract` (kurz, aber `as` klingt nach Cast) oder am Typ mit Klammern `with (Add, Subtract) by value, Show`
    (bleibt nah am Heutigen, beantwortet aber nur Frage 1). Sag ein Wort, dann stelle ich die 8 Stellen, beide
    Parser, den Checker (`derive.trb`/Delegation) und das Konzept um.
  - Antwort User: In dem Fall ist es ja immer noch verwirrend: Was, wenn ich Equals auf amount und currency anwende?
    Was, wenn ich Add auf Amount und Currency anwende? Das Verhalten ist total undurchsichtig.

    Eine Regel ist gut: Nur auf Single-Field-Typen anwendbar. Ein `Add by a, Subtract by b` auf einen Multi-Field-Typ ist nicht erlaubt.
    Damit ist es nicht sinnvoll, die Anweisung an das Feld zu machen.

    Ich finde glaube ich diese Syntax gut:, das ist schön explizit:

    ```trb
    type Seconds with Show, Add & Subtract by value, Compare by value {
      value: Int
    }

    Wie findest du die?
  - **Antwort: gut, besser als mein Vorschlag - nehmen wir.** Die Regel "nur Ein-Feld-Typen" nimmt dem Ganzen die
    Mehrdeutigkeit (bei `Money` müsste man sonst erklären, was `Add` mit `currency` macht - eben gar nichts
    Sinnvolles), und dann hat die Angabe am Feld keinen Vorteil mehr. Deine Schreibweise passt auch zum neuen `&`:
    `Add & Subtract by value` heißt "diese Gruppe kommt vom Feld `value`". Genau so:
    - `by` bindet an **das eine Element der `with`-Liste direkt davor**, und das Element darf eine `&`-Gruppe sein.
      `with Show, Add & Subtract by value, Compare by value` - `Show` wird abgeleitet, die anderen drei delegiert.
      Die Frage "worauf bezieht sich `by`" ist damit aus der Zeile beantwortet.
    - `by` ist nur an einem Typ mit **genau einem Feld** erlaubt, und das Feld muss das genannte sein (der Name
      bleibt stehen, obwohl er eindeutig ist: man liest, wohin es geht). Sonst Fehler: "`by` needs a type with a
      single field: `Money` has `amount` and `currency`".
    - Weitergereicht werden die geforderten Member, Defaults kommen vom Trait, ein Ergebnis vom Feldtyp wird wieder
      eingepackt (`Seconds + Seconds` ist `Seconds`) - wie bisher.
  - **Wird gelöst** (eigener Agent, nach dem laufenden Merge-Block): beide Parser (`with`-Liste: Element + optionales
    `by`), AST, Checker-Delegation mit der Ein-Feld-Regel, die 8 Stellen, Konzept ("Distinct Types") + Decision Log.
  - **Erledigt:** `by` hängt jetzt am einzelnen `with`-Element (auch als `&`-Gruppe) statt an der ganzen Liste, mit
    der Ein-Feld-Regel und den beiden Fehlermeldungen in Checker und beiden Parsern durchgesetzt, und Stage 0
    delegiert zur Laufzeit ebenso pro Trait.

- Oftmals wird z.B. `Ok(x)` statt `Ok x` geschrieben, ganz oft könnte man `Error a` oder `.SomeThing x, y, z` schreiben. Finde da am besten eine "Best Practice" die das strikt deklariert und an die sich alle halten und passe das rückwirkend auf der Codebase an (eventuell durch den Linter)
  - **Antwort:** Das Konzept hat dafür schon eine Formatter-Canon ("Command Calls"), sie ist nur nicht scharf genug
    und wird nirgends durchgesetzt. Meine Empfehlung geht in die andere Richtung als dein Beispiel:
    **Werte bekommen Klammern, Kommandos nicht.**
    1. **Wird der Wert des Aufrufs benutzt, stehen Klammern** - immer: `Ok(x)`, `Error(e)`, `Some(x)`, `.Case(x, y)`,
       `const email = Email.parse(text)`, `return Error(problem)`, der letzte Ausdruck einer Funktion. Grund:
       Kommandos schachteln nicht (`Ok Some x` ist verboten, es müsste `Ok(Some(x))` heißen) - dieselbe Konstruktion
       sähe je nach Tiefe verschieden aus; und `.SomeThing x, y` ist gar kein gültiges Kommando (ein Kommando beginnt
       mit einem Namen, `f .Case` ist der Member `f.Case`).
    2. **Steht der Aufruf als Statement für seine Wirkung da (`Void`), ist er ein Kommando:** `print "Hello"`,
       `printError message`, `name "acme/shop"`, `port 8080`, `test "adds" { ... }`, `unless done { ... }`.
    3. **Endet der Aufruf in einer Trailing Closure, ist er ein Kommando**, auch wenn der Wert benutzt wird:
       `const response = retry 3 { http.get(url) }` (steht schon so im Konzept).
    4. **Neu, macht die Regel mechanisch:** hat ein Argument einen Operator auf oberster Ebene, stehen Klammern:
       `assert(a == b)`, `print(count + 1)` - denn `assert a == b` liest sich als `(assert a) == b`. Das ist auch
       der Grund, warum im ganzen Repo `assert(...)` steht, und es bleibt so.
    5. Ohne Argumente immer `()`; erstes Argument beginnt mit `(`, `[`, `-`, `!`, `.` → Klammern (Grammatik).
  - **Wird gelöst:** Regeln 1-5 kommen als "Formatter canon" ins Konzept (ersetzt den heutigen Absatz), und die
    Codebase wird per Werkzeug über den Syntaxbaum umgestellt - zusammen mit dem Umformatieren der mehrzeiligen
    Strings, direkt nach dem 5.5-Merge. `torb lint`/`torb format` (Meilenstein 8) setzen sie danach durch. Wenn du
    Regel 1 andersherum willst (`Ok x` als Stil), sag es vorher - dann wäre die Konsequenz, dass Kommandos
    schachteln dürfen müssten, und das hat das Konzept bewusst ausgeschlossen.
  - Antwort user: Ist case nicht ein Type im Grunde? Müsste
  
    ```trb
    trait A {
      case B(x: Int)
      case C(y: String)
    }
    ```

    nicht technisch dasselbe sein wie

    ```trb
    trait A {}

    type B with A {
      x: Int
    }

    type C with A {
      y: String
    }
    ```

    Was ist mit `Ok Some(x)`? Die Konstruktoren sind doch quasi auch "Funktionen", ich kann doch auch so was wie
    ```trb
    ["admin", "moderator", "user"].map Role
    ```

    machen, oder?
  - **Antwort zu "ist ein Case nicht ein Typ?":** Semantisch nah dran (Scala/Kotlin machen es so: sealed trait +
    Klassen), aber in TorbScript bewusst nicht, aus drei Gründen. (1) **Geschlossenheit:** `type A { case B; case C }`
    ist abgeschlossen - daraus kommt die Exhaustiveness von `match`. Ein Trait ist offen: jedes Paket darf `type D
    with A` schreiben, ein `match` wäre nie vollständig. (2) **Darstellung:** ein `A` ist ein Wert fester Größe (Tag +
    größter Case, inline, ohne Dispatch); ein Trait-Wert ist Daten + Witness-Tabelle. (3) **Kein Subtyping:** wäre
    `B` ein Typ, bräuchte es `B` ist-ein `A` - die Sprache hat genau vier Coercions und keine Varianz, und das soll
    so bleiben. Wer einen Case mit eigener Identität will, macht ihn zum Typ und wickelt ihn ein:
    `case Circle(circle: Circle)` - dafür wird `From<Circle>` schon generiert, und `.Circle(c)` im Pattern gibt den
    Typ zurück.
  - **Zu `Ok Some(x)` und `.map Role`:** Du hast recht, ich war zu streng. Konstruktoren und Cases **sind**
    Funktionen: `names.map(Role)` geht (in der Aufräumrunde ist genau das passiert: `map { ModuleId(_) }` →
    `map(ModuleId)`), und `Ok Some(x)` ist grammatisch gültig - verboten ist nur `Ok Some x` (Kommandos schachteln
    nicht, die *Argumente* eines Kommandos sind gewöhnliche Ausdrücke mit Klammern). `["admin", "user"].map Role`
    geht dagegen nicht: ein Kommando beginnt mit einem Namen oder Member-Pfad, nicht mit einem Literal;
    `roles.map Role` ginge.
  - **Damit gibt es zwei in sich konsistente Canons - deine Wahl, ich wende dann eine strikt an:**
    - **K1 "Werte in Klammern":** `Ok(Some(x))`, `return Error(problem)`, `const role = Role(name)`,
      `names.map(Role)`; Kommandos nur für Wirkung (`print x`, `port 8080`) und mit Trailing Closure.
      Vorteil: eine Konstruktion sieht in jeder Tiefe gleich aus; "Kommando" heißt beim Lesen immer "hier passiert
      etwas".
    - **K2 "Kommando, wo die Grammatik es erlaubt":** `Ok Some(x)`, `return Error problem`,
      `const role = Role name`, `names.map Role`; Klammern nur, wo sie nötig sind (verschachtelt, in Argumenten,
      Operator im Argument, erstes Argument beginnt mit `(`/`[`/`-`/`!`/`.`). Vorteil: weniger Zeichen. Nachteil:
      dieselbe Konstruktion wechselt die Form mit der Position (`Ok Some(x)` außen, `Some(x)` innen), und
      `const role = Role name` liest sich nicht mehr als Wert.
    Ich empfehle K1. Das Werkzeug, das die Codebase umstellt, wartet auf deine Antwort.
  - **Entschieden (Chat, 2026-09-19): K2.** Die Canon, wie sie ins Konzept kommt und wie `torb format` sie später
    durchsetzt - ein Aufruf ist ein **Kommando**, wenn alles davon gilt:
    1. er steht in Kommandoposition (Anfang eines Statements, rechts von `=`, nach `return`, nach `=>`),
    2. der Aufgerufene ist ein Name oder Member-Pfad (`Ok`, `Email.parse`, `roles.map`),
    3. er hat mindestens ein Argument, und das erste beginnt nicht mit `(`, `[`, `-`, `!` oder `.`,
    4. kein Argument hat einen Operator auf oberster Ebene (`assert(a == b)`, `print(count + 1)` behalten Klammern),
    5. die Argumente passen auf eine Zeile (eine Trailing Closure darf über mehrere gehen).
    Sonst stehen Klammern - also immer innerhalb von Argumenten, Klammern, Operatoren und Listen
    (`Ok Some(x)`, nie `Ok Some x`), bei Aufrufen ohne Argumente (`list.length()`), und in den Köpfen von `if`,
    `for`, `while`, `match`. Beispiele: `Ok value`, `return Error problem`, `const role = Role name`,
    `const email = Email.parse text`, `names.map Role`, `print "Hello"`, aber `assert(sum == 3)`,
    `items.add(Item(name, 2))` → `items.add Item(name, 2)`.
  - **Wird gelöst:** Konzept-Absatz "Formatter canon" + Umstellung der ganzen Codebase per Werkzeug über den
    Syntaxbaum (zusammen mit dem Umformatieren der mehrzeiligen Strings), sobald die drei laufenden Zweige
    (Emitter, Prelude, `by`) gemergt sind - vorher würde es mit jedem davon kollidieren.

- In

```
public native type Char with Equals, Compare, Hash, Show {
  native fn isDigit(self): Bool
  native fn isLetter(self): Bool
  native fn isWhitespace(self): Bool

  /** The number of bytes of the character in UTF-8 (1 to 4): what it adds to an offset into a `String`. */
  native fn byteLength(self): Int
  native fn toUpperCase(self): Char
  native fn toLowerCase(self): Char

  /** `'A'`: inside of another value a character is written in single quotes, with escapes. */
  native fn showNested(self): String
}
```

was bedeutet es, wenn der Typ native ist gegenüber all seinen Methoden? Impliziert "native type", das all seine Methoden native sind? Wenn ja, wieso ist es hier doppelt markiert?
Wenn nicht, was bedeutet, bewirkt es?
  - **Antwort:** Es sind zwei verschiedene Aussagen, deshalb steht es doppelt.
    - `native type` heißt: **die Darstellung** kommt von der Runtime - der Typ hat keine Felder in TorbScript, der
      generierte Konstruktor entfällt, und die Traits hinter `with` liefert die Runtime mit (Lücke 22).
    - `native fn` heißt: **diese eine Funktion** kommt von der Runtime.
    `native type` impliziert also nicht, dass alle Methoden native sind: ein nativer Typ darf gewöhnliche Methoden
    haben, die in TorbScript auf den nativen aufbauen (`fn isEmpty(self): Bool { length() == 0 }`), und das ist genau
    die Richtung von "Natives klein halten": bei `Char` könnten `isDigit`, `isLetter`, `toUpperCase` TorbScript auf
    einem einzigen nativen `codePoint()` sein. Umgekehrt gibt es `native fn` auch an nicht-nativen Typen und frei
    (`print`). Im Konzept steht das nur halb ("Foreign Functions"); ich ergänze dort die zwei Sätze.


- Error trait/standardisierter Error type? Was ist mit stacktraces etc.?
  - **Stand heute:** Fehler sind gewöhnliche Werte, `Result<Value, Failure>` mit beliebigem `Failure`; `?`
    konvertiert über `From`. Es gibt keinen gemeinsamen Fehler-Trait und keine Traces. Für Bibliotheken ist das
    richtig (präzise Fehlertypen, `match` darauf), für Anwendungen fehlt "irgendein Fehler, reich ihn hoch".
  - **Vorschlag 1 - ein Trait, kein Basistyp:** `public trait Failure with Show { fn cause(self): Failure? { None } }`
    in der Prelude (der Name `Error` ist der Case von `Result`). Jeder Fehlertyp, der ihn implementiert, passt in
    `Result<Value, Failure>` - das ist dann der Trait-Wert (wie `anyhow::Error`/`Box<dyn Error>`), `?` verpackt
    über die vorhandene Coercion "Wert → Trait-Wert", und `cause()` gibt die Kette (`ConfigError` verursacht durch
    `IoError`). Generiert wird nichts automatisch außer dem, was es schon gibt (`Show`); `with Failure` schreibt man
    hin. `fn main(): Result<Void, Failure>` und das Top-Level-`?` drucken die Kette.
  - **Vorschlag 2 - Traces ohne Exceptions (wie Zigs "error return traces"):** ein Fehler ist ein Wert und trägt
    keinen Stack - das wäre teuer, nicht deterministisch und in Werten sichtbar. Stattdessen merkt sich im
    **Debug-Profil** jedes `?`, das einen Fehler weiterreicht, seine Quellstelle in einem kleinen Ringpuffer der
    Task. Endet das Programm an einem Top-Level-`?` (oder loggt man den Fehler mit `failureTrace()`), steht da:
    `error: config.trb: not found` / `  at src/config.trb:12:31` / `  at src/main.trb:5:22`. Kostet im
    Release-Profil nichts, braucht keine Änderung an Fehlertypen. Panics bekommen ihre Frames ohnehin (BACKEND 7.9).
  - **Deine Entscheidung:** beides so? Dann plane ich den Trait mit der nächsten Prelude-Runde ein und die
    `?`-Traces als Teil von 5.13/5.14 (Treiber-Profile), weil sie das Debug-/Release-Profil brauchen.
  - Vorschlag 1: Ja, aber ich will, dass der Trait Error heißt (wir brauchen bei use auch noch aliase for einzelne Typen)
  - Vorschlag 2: Auch ja, denke ich. Entscheide du
  - **Notiert, damit entschieden:**
    - Der Trait heißt **`Error`** (`public trait Error with Show { fn cause(self): Error? { None } }`). Das kollidiert
      heute mit dem Case `Error` von `Result`, den die Prelude als nackten Namen importiert. Lösung, die ohnehin
      fehlte: **ein Case und ein Typ dürfen denselben Namen tragen** - in einer Typposition werden nur Typen und
      Traits gesucht, in Ausdruck und Pattern gewinnt der Case. Das gleiche Problem hatten wir schon bei Wrapper-Cases
      (`case Circle(circle: Circle)` plus `use Circle from Shape`). `Result<Value, Error>` ist dann der Trait,
      `Error(problem)` der Case. Einzige Unschärfe: `Error.irgendwas` im Ausdruck meint den Case; wer den
      Namensraum des Traits braucht, importiert ihn unter einem Alias.
    - **Aliase in `use`:** `use Error as IoFailure, File from "std/fs"` - kommt mit derselben Runde (beide Parser,
      Scopes im Checker). `use * as fs from "..."` gibt es schon.
    - **`?`-Return-Traces:** ja, im Debug-Profil, als Teil von 5.13/5.14 (dort entstehen die Profile).
    Trait, Namensregel und Alias gehen in die Runde "Prelude-Umbau" (nächster Punkt), weil alle drei die Prelude und
    die Scopes anfassen.
  - **Geändert (Chat, 2026-09-19): der Case von `Result` heißt `Fail` statt `Error`** - `Result` ist `Ok(value)` oder
    `Fail(error)`. Damit kollidiert der Trait `Error` mit nichts, und die Namensregel "Case und Typ dürfen gleich
    heißen" **entfällt** (keine positionsabhängige Suche, wie von dir gewünscht; der Typparameter bleibt `Failure`).
    Mit K2 liest es sich auch gut: `return Fail problem`, `Ok value`. Der laufende Prelude-Agent hat beides
    bekommen: Namensregel streichen, Umbenennung über `std/`, `compiler/`, `examples/`, Tests, Stage 0, Checker-
    Meldungen, Konzept und Doku; er landet sie als letzten Schritt, damit sie nicht mit den anderen Zweigen kollidiert.
  - **Erledigt:** `public trait Error with Show { fn cause(self): Error? { None } }` steht in `std/core` und wird von
    allen Fehlertypen der `std/` getragen; der Case von `Result` heißt `Fail`, die Namensregel entfällt, `?`
    konvertiert einen konkreten Fehler über die vorhandene Coercion "Wert → Trait-Wert" in `Result<Value, Error>`, und
    Kette plus `?`-Return-Traces (Debug-Profil) sind als Regel in CONCEPT und als 5.13 in `docs/BACKEND.md` notiert.

- Das prelude enthält gerade jede menge Dinge, aber jede menge dinge auch nicht (math gehört z.B. ins prelude)
  Die ganzen Sachen im Prelude sollten in eigene, entsprechende Projekte und das Prelude sollte einfach mehrere dieser Projekte zusammenfassen
  und re-exportieren/laden bei denen wir der Ansicht sind "Das braucht man immer". Durch Erasure macht es ja kaum einen Unterschied, wie viel wirklich
  mit reinkommt. Aber so was wie JSON, Math, fs, environment, time etc. kann ruhig mit ins prelude auch.  - **Antwort:** Einverstanden mit dem Umbau, mit einer Einschränkung bei dem, was hineinkommt.
  - **Struktur (mache ich so):** die Prelude wird ein reines Re-Export-Paket. Der Inhalt zieht in eigene Pakete:
    `std/core` (`Option`, `Result`, `Void`, `Never`, Vergleichs-/Konversions-/Operator-Traits, `Range`, Kontrollfluss
    `do`/`unless`/`using`), `std/text` (`String`, `Char`), `std/number` (die Zahlentypen, `Bits`), `std/collections`
    (`List`, `Map`, `Set`, `Stack`, `Queue`, `Array`), `std/iteration` (`Iterable`, Stufen, Collectors),
    `std/encoding` (`Encode`/`Decode`), `std/expression` (`Expression`, `assert`), `std/task` (`Task`, `Channel`),
    `std/console` (`print`). `std/prelude/src/lib.trb` besteht dann nur noch aus `public use ... from "std/..."`.
    Der Compiler findet die Sprach-Typen (`Option` für `T?`, `Int64` für Literale) dann über die *Exporte* der
    Prelude statt über Dateien in `std/prelude` - das muss im Checker (`wellknown.trb`) mit umziehen.
  - **Was zusätzlich hinein sollte:** alles Reine, das man ständig braucht - `std/math`, die Zeit-*Werte*
    (`Duration`, `Instant`), `std/json` (nur Text ↔ Wert, keine IO).
  - **Wovon ich abrate: `std/fs`, `std/environment`, `std/process`, `Clock`, Netz.** Nicht wegen der Größe (da hast
    du recht, Ungenutztes kostet nichts), sondern weil der Import dort eine *Aussage* ist: (1) "diese Datei fasst
    Dateien an" steht oben in der Datei - für Reviews und für `torb add`, das Capabilities eines Pakets zeigt;
    (2) Receiver-Skripte und die Sandbox sind als "Prelude und sonst nichts" definiert (Lücke 34) - mit `File` in
    der Prelude bräuchte jede Sandbox eine zweite, beschnittene Prelude; (3) für JS/PHP-Targets gibt es die
    Capability-Tabelle: ein Import, der im Browser nicht existiert, ist ein klarer Compile error an einer Zeile -
    ein Name aus der Prelude, der mal da ist und mal nicht, ist es nicht. Man könnte Capabilities stattdessen über
    Erreichbarkeit berechnen (technisch präziser), aber die Lesbarkeit von (1) ginge verloren.
  - **Deine Entscheidung:** Prelude = Reines (inkl. `math`, `json`, Zeit-Werte), Capabilities bleiben Importe - ok?
    Den Umbau in Pakete starte ich unabhängig davon nach dem 5.5-Merge (er verschiebt nur Dateien und `use`-Zeilen,
    Stage 0 lädt `std/` nicht, der Checker prüft das Ergebnis).
  - Antwort User: Ja mach so
  - **Notiert, damit entschieden:** Prelude = reines Re-Export-Paket über `std/core`, `std/text`, `std/number`,
    `std/collections`, `std/iteration`, `std/encoding`, `std/expression`, `std/task`, `std/console`, dazu `std/math`,
    `std/json` und die Zeit-Werte; `fs`, `environment`, `process`, `Clock` und Netz bleiben Importe. Läuft als nächste
    Runde zusammen mit dem Trait `Error`, der Namensregel Case/Typ und den `use`-Aliasen.
  - **Erledigt:** die neun Pakete gibt es, `std/prelude/src/lib.trb` besteht nur noch aus `public use`-Zeilen (plus
    `Some`/`None`/`Ok`/`Fail`, `math` als Namensraum, `Json`/`JsonValue`/`JsonError` und `Duration`/`Instant`), der
    Checker findet die Sprach-Typen unverändert über die *Exporte* der Prelude, Zyklen zwischen std-Paketen sind
    unproblematisch (Fixpunkt, kein Dependency-Eintrag nötig), und `panic` sitzt in `std/core` statt in `std/console`.

- Wir brauchen ein Konstrukt für Bitmaks/Bitflags. Momentan gibt es keine Bit-Operatoren, wie könnte man so ein Konstrukt sauber implementieren, sodass es auch interoperabel konsumiert werden kann?
  - **Vorschlag - zwei kleine Bausteine statt eines Sonderkonstrukts:**
    1. **Cases mit festem Wert** (für jede Interop nützlich, nicht nur Flags): ein Typ, dessen Cases alle ohne Felder
       sind, darf ihnen Konstanten geben - alle oder keiner:

       ```trb
       type Permission {
         case Read = 1
         case Write = 2
         case Execute = 4
       }
       ```

       Generiert werden `rawValue(): Int` und `Permission.fromRawValue(2): Permission?`. Das ist das, was C-Enums,
       Protokoll-Codes und Datenbank-Spalten brauchen; FFI-Signaturen können so einen Typ direkt nehmen.
    2. **`Flags<Case>` in der std** - ein Wert-Typ über einer `UInt64`-Maske, mit der Mengen-Schnittstelle, die es
       schon gibt (`contains`, `added`/`add`, `removed`/`remove`, `union`, `intersection`, `isEmpty`, iterierbar
       über die gesetzten Cases), gebaut auf den vorhandenen `Bits`-Methoden - keine Bit-Operatoren in der Sprache:

       ```trb
       var access: Flags<Permission> = [.Read, .Write]
       access.add(.Execute)
       if access.contains(.Write) { ... }
       const mask = access.bits()                       // 7 - für FFI, Dateien, Netzwerk
       const parsed = Flags<Permission>.fromBits(5)?    // Fehler, wenn ein Bit zu keinem Case gehört
       ```

       Der Checker verlangt für `Flags<Case>`, dass jeder Wert eine Zweierpotenz ist (sonst Compile error am Case).
    - **Interop:** nach außen ist es genau die Zahl (`bits()`), im C-Backend ein `uint64_t` ohne Hülle, in JS ein
      `number` (bis 32 Flags verlustfrei mit den JS-Bitoperatoren, darüber zwei Hälften), in PHP ein `int`; in
      `Encode` wahlweise die Zahl oder die Liste der Case-Namen (Option des Formats).
    - **Entschieden (Chat, 2026-09-19): so wird es gemacht** - Cases mit festem Wert + `Flags<Case>`.
    - ~~Deine Entscheidung:~~ so? Dann plane ich (1) als Sprachfeature nach dem Fixpunkt ein (Parser, Checker,
      beide Backends) und (2) direkt danach in `std/collections`.

-   addNode(graph, DecisionNode.SwitchCase(path, owner, edges, otherwise)) könnte auch   addNode(graph, .SwitchCase(path, owner, edges, otherwise)) sein
  - **Antwort:** Ja - der Parametertyp ist der erwartete Typ, das ist gültiges TorbScript und läuft auch auf Stage 0
    (dort werden `.Case` an Parametern deklarierter Funktionen aufgelöst). Die Agents schreiben aus Vorsicht die lange
    Form, weil Stage 0 `.Case` nicht überall kann. Das per Hand nachzuziehen lohnt nicht: es ist eine Regel, die
    Typinformation braucht ("hier wird genau dieser Typ erwartet"), also eine **Lint-Regel mit Autofix** im
    Checker - kommt mit `torb lint` (Meilenstein 8) und wird dann einmal über das ganze Repo angewandt. In die
    CONTRIBUTING-Stilregeln nehme ich es jetzt schon auf, damit neuer Code die kurze Form nimmt, wo Stage 0 sie kann.

- (Chat, 2026-09-19) Eine Property lässt sich beim Refactoring nicht durch einen Getter ersetzen?
  - **Antwort / entschieden:** Richtig, und es bleibt so: keine Getter/Properties. Ein Feld ist Teil der öffentlichen
    Form eines Typs an fünf Stellen (Konstruktor, Patterns, `copy`, `Encode`/`Decode`, `var`-Pfade) - eine Property
    würde nur das Lesen abfangen. Faustregel: was später berechnet werden könnte, ist von Anfang an eine Methode.
    Für den Umbau `.x` → `.x()`: `deprecated` am Feld + Auto-Fix in `torb lint --fix` und als Quick-Fix im LSP
    (Meilenstein 8). Steht im Konzept unter "Visibility and Encapsulation" und bei den Open Questions (`deprecated`).

- (Chat, 2026-09-19) Die `.` vor den Cases stören (`.None =>`, lange `match`-Listen). Kontextabhängig weglassen?
  - **Entschieden (Nutzer):** Nein, der Punkt bleibt überall - `.Case` im Pattern wie im Ausdruck. Geprüft wurden
    Rust (nackter Name über den Scope: vertippter Case wird zum Catch-all, genau das haben wir abgeschafft), Haskell/Elm
    (Großschreibung entscheidet im Pattern) und "Case des erwarteten Typs gewinnt" im Ausdruck (Bedeutung von
    `Transform(a, b)` hinge vom erwarteten Typ ab). Nur im Pattern ohne Punkt wäre gegangen, aber dann lieber
    einheitlich: ein Case hat immer einen Punkt oder seinen Typ davor.
  - **Zurückgestellt (nach dem Fixpunkt, als Vorschlag):** `const Some(x) = … else { return … }` - nimmt die häufigste
    hässliche Form weg (ein `match`, nur um bei `.None` auszusteigen; `.None =>` steht 317-mal in `compiler/src`).
    Kommt nicht in Stage 0 (keine neuen Sprachfeatures dort), sondern danach mit Beispielen zur Entscheidung.
  - **Nachtrag (Nutzer: "None sollte dann halt mit ins Prelude") - wird gelöst:** `None` IST schon in der Prelude
    (`use Some, None from Option`), im Ausdruck steht es 260-mal nackt. Die Lücke ist nur das Pattern: dort bindet ein
    nackter Name immer, also musste es `.None` heißen, während `Some(x)` wegen der Klammern geht. Neue Regel: **ein
    importierter Case gilt nackt überall, auch im Pattern; eine Bindung beginnt klein (Compile-Fehler statt Lint).**
    Damit bleibt die alte Falle zu: ein vertipptes `Nome` ist kein Catch-all, sondern "kein Case im Scope". Es hängt
    am Scope (`use`), nicht am erwarteten Typ - für alles nicht Importierte bleibt `.Case`. Also `Some(found) =>` /
    `None =>`, `Ok(value) =>` / `Fail(problem) =>`. Umsetzung (beide Parser, Checker, Stage 0) und Umstellung des
    Bestands im Werkzeuglauf zusammen mit K2, nach den drei Merges.
  - **Erledigt:** in beiden Parsern entscheidet jetzt der erste Buchstabe - ein großgeschriebener Pattern-Name wird
    derselbe Knoten wie `Shape.Circle` (`Variant`, ein Segment, keine Felder), ein kleingeschriebener bindet, `_Found`
    und `_` behalten ihre Regel. Der Checker löst ihn über den Scope auf und meldet sonst "`Nome` is not a case in
    scope" mit der Regel als Note und dem nächstliegenden Case des gematchten Typs; Stage 0 sucht den Namen zur
    Laufzeit und scheitert laut. Der Bestand (`.None =>`) ist absichtlich nicht umgestellt: das macht `torb canon` mit
    der Regel `imported-case-patterns`, die jetzt eingeschaltet werden kann.
  - **Erledigt (Nachtrag):** ein nackter Case muss jetzt auch ein Case des gematchten Typs sein - `Fail =>` gegen
    `DecisionNode` (das selbst einen Case `Fail` hat) ging vorher durch, weil der Checker nach dem Scope-Lookup nur noch
    über den Namen verglich, während Stage 0 `Result.Fail` auflöst und kein Arm passt (genau das hat 11 Tests
    zerbrochen). Meldung, Note und Tests in `compiler/src/semantics/checker/pattern.trb` bzw.
    `compiler/tests/statements.test.trb`, Entscheidung als Lücke 46 in `docs/TYPECHECKER.md`.

- (Chat, 2026-09-19) Cases importieren: `use Option.* from "…"`, `use Option.Some, Option.None from "…"`,
  `use Option.{Some, None} from "…"`?
  - **Entschieden:** Die Pfad-Form kommt und ERSETZT `use Some, None from Option`:
    `use Option, Option.Some, Option.None from "./option"`. Damit steht hinter `from` immer ein Modul (heute: Modul
    oder Typ), ein Schritt statt zwei, und der Import liest sich wie der Name im Code (`Option.Some`, nur gekürzt);
    `as` geht wie überall (`use Option.None as Nothing from …`). Ohne `from` gilt der Pfad im eigenen Scope
    (`use Shape.Circle`). Nur Cases, keine Methoden (kein UFCS, keine zweite Schreibweise für denselben Aufruf;
    als Wert reicht `Option.map`). Betroffen: 11 Stellen (Prelude, Tests, `language.trb`), beide Parser, Resolver.
    Umsetzung zusammen mit "importierter Case nackt im Pattern", nach dem Merge der Prelude-Runde.
  - **Nein zu `Option.*`:** die einzige Import-Form, bei der sich eine Datei ändert, ohne dass sie jemand anfasst
    (neuer Case in der Abhängigkeit → neuer Name im Scope, kollidiert z. B. mit dem Typ `Transform` neben
    `Component.Transform`). Seit großgeschriebene Pattern-Namen über den Scope laufen, muss der Scope oben ablesbar sein.
  - **Nein zu `Option.{Some, None}`** (mein Votum, sag Bescheid wenn du sie doch willst): eine zweite Grammatikform,
    die nur ein wiederholtes `Option.` spart - bei den wenigen Stellen, an denen Cases importiert werden, zu wenig.
  - **Erledigt:** `UseItem.name` ist `UseItem.path` geworden und `UseSource.Type` ist `UseSource.Local`, also löst ein
    Durchlauf jede Form auf: erstes Segment = Export des Moduls oder Name dieser Datei, jedes weitere = Case eines Typs
    oder Name hinter einem Namensraum-Import (`use shapes.Shape.Circle`). Die alte Form ist ein Parserfehler, der die
    neue nennt; eine Methode, eine Konstante oder ein Feld bekommt "Only a case can be imported from a type: `area` is
    a method of `Shape`". Umgestellt: Prelude, die drei Test-Preludes, `language.trb`, `errors.trb`, CONCEPT,
    CONTRIBUTING, `bootstrap/README.md`.

- (Fund beim Prelude-Umbau, 2026-09-19) `Result<Void, Error>` erfüllt `Show` nicht.
  - **Wird gelöst:** Checker-Lücke: ein Trait-Wert erfüllt eine Bound, die sein eigener Trait verlangt
    (`trait Error with Show`), bisher nur bei der Koerzion, nicht als Bound-Witness
    (`extend<Value: Show, Failure: Show> Result<…> with Show`). Geht mit der Syntax-Runde (Case-Import per Pfad,
    importierter Case nackt im Pattern) an denselben Agent.
  - **Erledigt, andere Ursache:** der Trait-Wert als Bound-Witness ging schon (`Result<Int, Error>` ist `Show`, auch
    `Result<Int, Show & Hash>`) - er war nur nicht getestet. Kaputt war `Void`: eine Signatur schreibt es als den Typ
    der Sprache (`TypeForm.VoidType`), `with Equals, Hash, Show` steht aber am `native type Void` von `std/core`, und
    nichts verband die beiden - `Void` erfüllte also gar keinen Trait, weshalb das `Value: Show` der `Result`-
    Implementierung scheiterte. Trait-Auflösung und Trait-Hülle fragen jetzt diese Deklaration (`declaredVoidOf`), was
    auch `nothing.show()` zu einem normalen Memberaufruf macht. Tests halten die genauen Witness-Bäume fest
    (`Result<Value, Failure>: Show [Void: Show, object Problem: Show]`); Entscheidung in `docs/TYPECHECKER.md` als
    Lücke 43.

- (Chat, 2026-09-19) Wieso `Void` → `Void`, aber `Bool` → `true`/`false`? Wäre `void` nicht besser (`Ok void`)?
  - **Entschieden - wird gelöst:** Ja. Der Wert von `Void` ist das Literal `void`, so wie `true`/`false` die Werte
    von `Bool` sind: `Ok void`, `return void`. Die bisherige Form kam von Kotlins `Unit` (Typ und Singleton-Objekt
    gleichen Namens) - dort trägt das `object`-Konzept sie, TorbScript hat keine Singletons, also war es die eine
    Ausnahme von "ein großgeschriebener Name ist ein Typ oder ein Case". Mit der neuen Regel "Bindungen klein, im
    Pattern ist Großes nie eine Bindung" wäre sie noch schiefer geworden. `Void` an Wertposition wird ein Fehler, der
    die neue Schreibweise nennt. Kosten: ein Schlüsselwort, ~16 Stellen + Doku; läuft in der Syntax-Runde mit
    (Case-Import per Pfad, importierter Case nackt im Pattern, Bound-Lücke bei Trait-Werten).
  - **Erledigt:** `void` ist Schlüsselwort in beiden Lexern, Literal-Ausdruck und Literal-Pattern in beiden Parsern,
    `VoidLiteral` in beiden Dumps; `Resolution.VoidValue` ist mit dem Namen weggefallen, für den es stand. `Void` an
    Wertposition sagt "`Void` is a type, not a value: its one value is written `void`". Ein Schlüsselwort kann kein Name
    sein: die 88 + 15 lokalen Variablen, die `void` hießen (fast alle `const void = program.internType(...)` in den
    IR-Tests), heißen `voidType`. Nicht angefasst: `Show` von `Void` - Stage 0 druckt `Void`, das C-Runtime `()`, diese
    Abweichung gab es vorher schon und gehört zu 5.14.

- (Chat, 2026-09-19) `*` oder `?` statt `_` als Wildcard im Pattern, weil `_` schon der implizite Closure-Parameter ist?
  - **Entschieden (Nutzer):** `_` bleibt für beides. `?` hat schon drei Bedeutungen, `*` liest sich als Glob/Operator,
    `_` ist als Wildcard überall Konvention (~1300 Stellen). `it` für den Closure-Parameter wurde erwogen und verworfen.
    Die Positionen überlappen nie; `match _ { … }` kommt im Repo nicht vor.

- (Chat, 2026-09-19) `inclusive` statt `isInclusive` - "isser" sind Methoden.
  - **Entschieden:** Ja, und es passt zur Feld/Methode-Trennung ("ein Feld ist ein Versprechen über Daten"): ein
    `Bool`-Feld (auch Parameter, Bindung) ist ein Adjektiv oder Partizip (`inclusive`, `discarded`, `signed`), eine
    Frage, die berechnet wird, ist eine Methode mit `is`/`has` (`isEmpty()`, `hasGuard()`). Am Aufruf sieht man dann
    ohne Klammern-Raten, was Daten sind. Kommt als Namensregel ins Konzept und in CONTRIBUTING.
  - **Haken:** im Compiler heißen viele Flags nach Schlüsselwörtern (`isVar` 20-mal, `isStatic`, `isPublic`, `isNative`,
    `isShared`, `isConst`) - `var: Bool` geht nicht. Dort wird pro Name entschieden: ein anderes Wort (`mutable`,
    `exported`) oder besser gleich ein Typ statt eines Flags (`Visibility` gibt es schon). Das sind ~60 Namen an 135
    Stellen, keine mechanische Umbenennung.
  - **Wird gelöst in zwei Schritten:** (1) öffentliche std-API jetzt - das ist genau eine Stelle, `Range.inclusive`
    (+ Stage 0, Runtime, 4 Stellen im Compiler), in der nächsten ruhigen Runde zusammen mit der Canon-Anwendung.
    (2) Compiler-interne Namen nach dem Fixpunkt mit dem Methoden-Umbau, dann mit geprüftem Rename statt Textersetzung.
  - **Erledigt:** `Range.inclusive` in `std/core`, `compiler/tests/harness.trb`, CONCEPT und `docs/TYPECHECKER.md`; die
    Namensregel steht in CONCEPT ("Lexical Structure") und in `compiler/CONTRIBUTING.md`, wo auch steht, warum die
    compiler-internen `is...`-Felder bis nach dem Fixpunkt bleiben. Stage 0 und `runtime/` waren nicht betroffen: der
    Interpreter normalisiert `0..=10` beim Auswerten und kennt kein Feld dieses Namens.

- (Chat, 2026-09-19) Case-Syntax: ADT als "closed trait + Typen", `type Point(Int, Int)`, `type Point3(x: Int, …)`,
  Methoden pro Case (`case Circle(…) { fn area(self) … }`), eigenes Keyword für ADTs?
  - **Antwort (Empfehlung, Entscheidung beim Nutzer):**
    1. *Cases als Typen:* als Erklärmodell richtig, als Semantik nein. `closed` löst die Geschlossenheit, die
       Trait-Koerzion das Subtyping - was bleibt, ist die **Inferenz**: sobald `Circle(1.0)` den Typ `Circle` hat,
       brauchen `shape = Rect(…)` nach `var shape = Circle(1.0)`, `[Circle(…), Rect(…)]`, `if … { Circle } else { Rect }`
       und `Some(1)`/`None` einen Join (kleinster gemeinsamer Obertyp) - genau das Subtyping in der Inferenz, das die
       Sprache nicht hat (Scalas `Some[Int]`-Problem, Rusts liegengebliebener RFC zu Variant Types). Dazu: `None`
       müsste `Option<Value>` für jedes `Value` sein, und Cases würden Modul-Namen (`Transform` Case vs. Typ).
    2. *Methoden pro Case:* nein - zweite Schreibweise für `match self`, gleiche Exhaustiveness, braucht eine neue
       Regel für nackte Feldnamen. Wer Glieder mit eigener Identität und eigenen Methoden will, will eigentlich
       **`closed trait`** (nur im eigenen Paket implementierbar → `match` über Typ-Patterns ist vollständig). Das wäre
       das ehrliche Feature, additiv, ohne `case` anzufassen - als offene Frage für nach dem Fixpunkt notiert.
    3. *`type Point3(x: Int, y: Int, z: Int)`:* ja. Spiegelt `case Circle(radius: Float)` und den Konstruktoraufruf.
       Regel: Felder stehen im Kopf ODER im Block, nie beides; der Block hinter dem Kopf hält nur Methoden. Die
       Blockform bleibt für lange/dokumentierte Records. Reiner Parser-Zucker, nach dem Fixpunkt (dann nur ein Parser).
    4. *`type Point(Int, Int)` mit `.0`/`.1`:* nein. Ein benannter Typ ist für Namen da, für Positionen gibt es Tupel;
       `self.0 * self.0` widerspricht "Namen ausgeschrieben", und `_`, `_2` als erfundene Parameternamen gäben `_` eine
       dritte Bedeutung. Der Newtype-Fall ist mit 3. kurz genug: `type Meters(value: Float) with Add & Subtract by value`.
    5. *Keyword:* `type` bleibt - ein Typ mit Cases.
  - **Entschieden (Nutzer):** Kopf-Form und `closed trait` kommen beide erst mal NICHT. `Point(x: Int, y: Int)` und
    `Point { x: Int, y: Int }` sind sich zu ähnlich - der Nutzer fragt sich dann immer, was besser ist. Es bleibt bei
    der Blockform als einziger Schreibweise; der ganze Punkt ist damit zurückgezogen.

- (Chat, 2026-09-19) `const list: List<Void> = [void, void, void, void]` - ist das ein Schlupfloch?
  - **Antwort:** Nein, das ist die Folge davon, dass `Void` ein gewöhnlicher Typ mit genau einem Wert ist - und das
    ist gewollt. Geht heute (Stage 0: Länge 4, `[Void, Void, Void, Void]`), geht in Rust genauso
    (`vec![(); 4]`, ein Typ der Größe null; `HashSet<T>` IST dort `HashMap<T, ()>`). Das echte Schlupfloch haben die
    Sprachen, in denen `void` KEIN Typ ist: Javas `Void`/`null`, C#s doppelte `Action`/`Func`, keine
    `Result<void, E>`. TorbScript braucht `Result<Void, Error>`, `Task<Void>`, `Channel<Void>` (Signal) - also muss
    auch `List<Void>` gehen. Sinnlos, aber harmlos; ein Lint-Hinweis wäre möglich, ein Verbot wäre eine Sonderregel.

- (Chat, 2026-09-19) `match { … }` ohne Subjekt als Kurzform für `match _ { … }` im Closure?
  - **Entschieden:** Nein. `match _` steht im Repo 0-mal, ein Closure, das sofort seinen Parameter matcht, 2-mal; zwei
    erlaubte Schreibweisen wären wieder "was ist besser?"; ein subjektloses `when`/`switch` heißt in Kotlin und Go
    "Kette von Bedingungen". `_` macht sichtbar, was gematcht wird. Nachrüstbar, weil `match {` heute ein Syntaxfehler ist.

- (Chat, 2026-09-19) Der HTTP-Body soll ein Stream sein; ein sinnvolles Streaming-Protokoll (wie in Rust, aber
  ergonomisch, stabil, in die Sprache integriert); JSON und alle anderen Encoder/Decoder müssen Streams nativ können.
  - **Entschieden (Design, ich) - wird gelöst:**
    1. **Ein Protokoll, pull-basiert:** `trait Stream<Item> { fn next(self): Task<Item?> }` - das asynchrone
       Gegenstück zu `Iterator` (`next(var self): Item?`). Implementierer sind `shared type`s (ein Stream hat
       Identität und wird verbraucht; `var self` über ein `await` hinweg wäre ein offener Exklusiv-Zugriff).
       `Channel<Item>` IST ein Stream (`receive` = `next`) und die Brücke für Produzenten, die schieben. Kein
       `Sink`/`AsyncWrite`: geschrieben wird, indem man einen Stream übergibt (Request-Body, `File.write(stream)`) -
       Backpressure ergibt sich aus dem Ziehen. Kein `Pin`, kein `poll`, keine Sync/Async-Dopplung von Read/Write.
    2. **In der Sprache:** `for chunk in stream { … }` geht in Funktionen, die `await()` dürfen (dieselbe Regel wie
       für `await()`); kein neues Schlüsselwort. Stufen wie bei `Iterable` als Default-Methoden des Traits (`map`,
       `filter`, `take`, `collect(collector)` - `Channel.collect` gibt es schon), `Stream.from(iterable)`.
    3. **Fehler:** Elemente sind `Result` (`Stream<Result<List<UInt8>, HttpError>>`), wie in Rust; `chunk?` im Rumpf.
    4. **HTTP:** `Response.body: Body`, `shared type Body with Stream<…>, Close`; `body.bytes(limit:)`, `.text()`,
       `.json<Value>()` (alle `Task`, mit Größenlimit - wichtig für Server), `.items<Item>(format)`;
       `Body.from(text|bytes|stream)`, `Body.json(value)`, `Body.empty()`. `response.json<User>()` wird `Task`.
    5. **Encode/Decode bleiben synchron und unverändert** (abgeleiteter Code, keine "gefärbten" Decoder - das ist die
       Stabilität). Streaming passiert auf der Ebene, auf der es in der Praxis vorkommt - dem **Element**: jedes Format
       implementiert `trait Format` mit (a) ganzem Wert aus Bytes, (b) `decodeItems<Item>(source): Stream<Result<Item, …>>`
       und `encodeItems<Item>(items): Stream<List<UInt8>>` (JSON-Array, NDJSON, MessagePack-Folgen, CSV-Zeilen, SSE):
       ein wiederaufnehmbarer *Framer* des Formats findet die Elementgrenze im Puffer, zieht bei Bedarf asynchron nach,
       das Element selbst wird synchron dekodiert - Speicher = ein Element. (c) später Ereignis-Ebene
       (`Json.events`, SAX-artig) für beliebig große Einzeldokumente. Ein einzelner Riesenwert in ein `struct` zu
       streamen spart nur den Textpuffer (der Wert liegt ohnehin im Speicher) und würde jeden Decoder färben: nein.
    6. **Wenig Natives:** nativ sind nur die Quellen (Socket, Datei-Chunks); Trait, Stufen, Framer, `Body`-Helfer
       sind TorbScript.
  - **Ablauf:** Spezifikation (CONCEPT "Streams", `docs/`) + Deklarationen in `std/task`, `std/encoding`, `std/json`,
    `std/http`, `std/fs` + Checker-Regel für `for` über einen Stream, sobald Syntax-Runde und Canon-Anwendung
    gemergt sind (beide fassen `std/` an). Laufzeit (Tasks, Sockets) kommt mit Meilenstein 7 bzw. 10.
  - **Nachtrag (Nutzer: "auch FS und io sollen das Protokoll sauber nutzen"):** Ja, ein Protokoll für alle Quellen.
    `std/fs`: `file.chunks(size:)`, `file.lines()` als `Stream<Result<…, IoError>>` (heute liefert `File.lines` ein
    `Iterable<String>` und verschluckt Lesefehler mitten in der Datei), `File.write(path, stream)`/`file.write(stream)`;
    die Ganz-Datei-Helfer (`readText`, `writeText`) bleiben als Kurzform. `std/io`: Standard-Eingabe als Stream
    (`input.lines()`, `input.chunks()`), Standard-Ausgabe/-Fehler nehmen einen Stream; `readLine()` bleibt als Kurzform.
    `std/process`: Ein-/Ausgabe eines Kindprozesses sind dieselben Streams. Text über Bytes: eine Stufe
    `lines()`/`text()` auf `Stream<…List<UInt8>…>`, die UTF-8 über Chunk-Grenzen hinweg richtig zusammensetzt.
  - **Nachtrag (Nutzer: "Was ist mit Writable Streams?") - Korrektur von Punkt 1:** "Kein Sink" war zu knapp
    geschnitten. Einen Stream zu übergeben deckt das *Übertragen* ab (Upload, Kopieren), aber nicht den imperativen
    Produzenten (Log, SSE, Report: schreiben - rechnen - schreiben) und nicht generischen Code, der "in irgendetwas"
    schreibt; dafür jedes Mal `Channel` + `spawn` wäre umständlich. Also die zweite Hälfte, in der Form, die `Channel`
    schon hat:
    `trait Sink<Item, Failure> { fn send(self, item: Item): Task<Result<Void, Failure>>; fn close(self): Task<Result<Void, Failure>> }`.
    - **Backpressure = das `await` auf `send`** (fertig, wenn das Ziel das Element angenommen hat) - wie `Channel` mit
      Kapazität. Kein `poll_ready`/`start_send`/`poll_flush` wie bei Rusts `Sink`. Puffern ist ein ausdrücklicher
      Wrapper (`Buffered`) mit eigenem `flush()`; `close()` leert ihn.
    - **`Failure` als zweiter Typparameter** (keine assoziierten Typen in der Sprache); ein Ziel, das nicht scheitern
      kann, ist `Sink<Item, Never>`. Beim Lesen bleibt der Fehler im Element (`Stream<Result<…>>`), weil es viele
      Streams ohne Fehler gibt (Channel, `Stream.from(iterable)`).
    - **`Channel<Item>` ist beides:** `Stream<Item>` und `Sink<Item, ChannelClosed>` - zwei verbundene Enden.
    - **Verbinden:** `sink.sendAll(stream, close: true)` zieht den Stream leer. Bequemlichkeit auf Byte-Zielen als
      Erweiterung: `sendText(text)`, `sendLine(text)`.
    - **Wer es ist:** `File` (`Sink<List<UInt8>, IoError>`), Standard-Ausgabe/-Fehler, die Eingabe eines Kindprozesses,
      Socket, später der Antwort-Body im HTTP-Server. Formate brauchen nichts Neues: `sink.send(Json.encode(value))`
      bzw. `sink.sendAll(Json.encodeItems(items))`.
  - **Konzept v2 (nach dem Vergleich Rust/C#/Swift/Scala/Java/Node/Web/Bun; ersetzt Punkt 1-3 und den Sink-Nachtrag):**
    "Stream" ist das Wort für den einseitig gerichteten Fluss (Kapitel, Paket `std/stream`), kein Typ. Ein Stream hat
    zwei Enden, und die heißen als Paar: **`Source<Item, Failure>`** (`next(self): Task<Result<Item?, Failure>>`) und
    **`Sink<Item, Failure>`** (`add(self, item): Task<Result<Void, Failure>>`, `finish(self): Task<Result<Void, Failure>>`).
    Das sind die asynchronen Geschwister von `Iterator` (`next`) und `Accumulator` (`add`, `finish`) - dieselben Verben.
    - Fehler beendet den Stream (wie überall außer Rust) und steht im Typ, symmetrisch auf beiden Enden; `Never` für
      Enden, die nicht scheitern. Backpressure: Ziehen an der Quelle, `await` auf `add` am Ziel.
    - `Channel<Item>` ist ein Stream im Speicher, von dem man beide Enden hält: `channel.source`, `channel.sink`
      (getrennt weitergebbar). Alles Bidirektionale (Socket, Kindprozess, WebSocket) ist ein Typ mit `source` und `sink`.
    - **Verzahnung mit `Iterable`:** (1) dieselben Verben und Stufennamen (`map`, `filter`, `take`, …);
      (2) `Collector` wird geteilt: `source.collect(collector)`, `toList()`, `fold`, `joined` - Endoperationen einmal
      geschrieben; (3) **`Stage<Input, Output, Failure>`** - synchrone, wiederaufnehmbare Mittelstufe (`add(var self,
      input): Result<List<Output>, Failure>`, `finish`) für Framing und Codecs (UTF-8-Zeilen, `Json.items<User>()`,
      gzip), anwendbar auf Quelle UND Iterable per `through(stage)`; das Wort "stage" benutzt `std/iteration` schon;
      (4) Brücken `Source.from(iterable)` und `source.toList()`.
    - **Produzieren ohne Generatoren:** `Source.from(iterable)`, `Source.from { … }` (Closure = `next`),
      `Source.produce { sink => … }` (Task + Übergabe-Channel, Kapazität 0 = Generator-Gleichschritt). `yield` bleibt
      offene Frage für später (dieselbe Zustandsmaschine wie `Task`).
    - **Schleife:** `while const Some(line) = lines.next().await()? { … }` - geht heute, `await` und `?` sichtbar.
      Bewusst kein `for` über eine Quelle in v1: es gäbe keinen Platz für das `?` (Swift braucht dafür `for try await`).
    - `Bytes` = Alias für `List<UInt8>`, Chunks gehören dem Produzenten (kein Aufrufer-Puffer über ein `await`).
    - HTTP, fs, io, process, Formate wie oben, nur mit `Source<Bytes, …>`/`Sink<Bytes, …>`; `Format.items<Item>()` und
      `Format.encoded<Item>()` liefern `Stage`s. Bequemlichkeit zuerst (Bun): `body.text()`, `body.json<User>()`,
      `File.write(path, source)`; der Stream ist die Ebene darunter.
    - Offen beim Nutzer: die Namen `Source`/`Sink` (Alternative `Producer`/`Consumer`).
  - **Entschieden (Nutzer):** Die Namen sind `Source` + `Sink` neben `Iterator` + `Accumulator`.
  - **Konzept v3 - die Stufen nur einmal (Nutzer: "Konzepte nahe zusammenbringen, Collector passt zu beiden"):**
    Eine Stufe hängt nicht an der Quelle, sondern am Ziel: `trait Stage<Input, Output> { fn onto<Final>(self,
    downstream: Accumulator<Output, Final>): Accumulator<Input, Final> }` - sie macht aus einem Accumulator einen
    Accumulator (Clojure-Transducer; genau so sind Javas Streams intern gebaut). `map`, `filter`, `take`,
    `takeWhile`, `skip`, `flatMap`, `indexed`, `lines`, `Json.items<User>()` gibt es damit EINMAL, synchron,
    quellenunabhängig; `stage.then(other)` macht Pipelines zu Werten, die auf eine Liste wie auf einen HTTP-Body passen.
    - `Accumulator` bekommt `isDone(self): Bool` (Default `false`) für `take`/`first`/`find`.
    - Der Unterschied synchron/asynchron schrumpft auf zwei **Treiber**: `Iterable.through(stage)`/`collect` (Schleife)
      und `Source.through(stage)`/`collect` (Schleife mit `await`). `map`/`filter`/… auf beiden Traits sind Einzeiler
      `through(Stage.map(transform))` (ohne Higher-Kinded Types lassen sich die Namen nicht teilen, die Logik schon).
    - Ziehen aus einer gestuften Pipeline (`for`, `zip`) läuft über eine kleine Warteschlange (Clojures `sequence`);
      `collect` läuft verschmolzen ohne sie. `zip` (zwei Quellen) bleibt Treibersache, `sorted` ist Sammeln + neu Liefern.
    - Stufen, die scheitern können, liefern `Result`-Elemente; `source.checked()` hebt sie in den `Failure` des
      Streams, auf Iterables sammelt das vorhandene `Result … with From<Iterable<…>>`.
    - Nur asynchron bleibt, was warten muss: `Source`, `Sink`, `source.then { … }` (Stufe mit Task-Funktion).
    - Verworfen: alles asynchron (fs2/Web - tötet `list.map(…).toList()`), Effekt-Polymorphie/HKT (Rusts ungelöste
      "keyword generics"), blockierendes `await` mit Stack pro Task (Go/Loom - im Konzept schon abgelehnt, JS-Target).
    - Folge: `std/iteration/stages.trb` wird auf `Stage`-Werte umgebaut (Stage 0 lädt `std/` nicht, Risiko klein).
  - **Entschieden (Nutzer):** v3 gilt erst mal so. Spezifikation + std-Deklarationen + Umbau von `std/iteration` starten
    nach dem 5.6-Merge und der Canon-Anwendung.
  - **Erledigt:** `docs/STREAMS.md` ist die Spezifikation, CONCEPT hat das Kapitel "Streams" plus Decision Log und offene
    Fragen, und `std/stream` (`Source`, `Sink`, `Pulling`, `Pushing`, `Buffered`, `Staged`, `Bytes`, `lines`) steht
    zusammen mit `Stage` + `Accumulator.isDone` + `Iterable.through` in `std/iteration`, `Channel.source()`/`sink()` in
    `std/task`, `trait Format<Failure>` in `std/encoding` mit einem in TorbScript geschriebenen JSON-Framer, `Body` als
    `Source<Bytes, HttpError>` in `std/http`, `File` als `Sink<Bytes, IoError>` mit `chunks()`/`lines()`/`create`/`write`
    in `std/fs`, den drei Standardströmen in `std/io`, `Process.start`/`Child` in `std/process`, Prelude-Re-Exports und
    `examples/tour/src/13-streams.trb`; der Umbau von `std/iteration/stages.trb` auf `Stage`-Werte wartet bewusst auf das
    Backend (STREAMS.md Abschnitt 14, Punkt 6).

- (Chat, 2026-09-20) Eine saubere Dokumentationsform für Menschen und Agents: Markdown mit Frontmatter (`title`,
  `summary`, …), Index-Dateien pro Ordner und Index der Indexe, daraus am Ende ein Agent Skill; Best Practices recherchieren.
  - **Wird gelöst, in zwei Phasen.** Phase 1 (Opus-Agent, läuft): Recherche (Diátaxis, Doku anderer Sprachen,
    `llms.txt`, Anthropics Agent-Skills-Format, retrieval-freundliches Schreiben, getestete Snippets) →
    `docs/contributing/research.md`; Baum unter `docs/` (`guide`, `language`, `standard-library`, `how-to`,
    `explanation` inkl. "coming from Rust/Swift/Kotlin/TypeScript", `tooling`, `internals`, `contributing`, Glossar);
    Frontmatter-Schema; Seitenvorlagen je Art; Schreibregeln; Muster-Seiten; Seiteninventar für Phase 2.
  - **Meine Festlegungen:** (1) Index-Dateien werden aus `title`/`summary`/`order` der Kinder **generiert** (zwischen
    Markern; eigene Frontmatter und Einleitung bleiben) - ein Index kann nie veralten, ein Agent geht Index →
    Summary → Seite. (2) **Jeder `trb`-Block wird geprüft** mit dem Front-End des Compilers (parst + Canon;
    markierbar als typgeprüft, Fragment, oder absichtlicher Fehler mit erwarteter Meldung) - falsche Beispiele sind
    für Agents das Schlimmste. (3) Das Werkzeug ist TorbScript im Compiler-CLI: `torb docs check|index|skill|bundle`,
    wird Gate. (4) `status: planned` trennt Geplantes von Vorhandenem. (5) `CONCEPT.md` bleibt bis zum Fixpunkt die
    Design-Quelle, Seiten nennen ihre `source`; die bestehenden `docs/*.md` bleiben vorerst an ihrem Platz
    (laufende Agents schreiben hinein) und werden über `docs/internals/` verlinkt.
  - Phase 2: mehrere günstigere Schreib-Agents füllen die Ordner nach Vorlage und Inventar; danach der Skill
    (`SKILL.md` mit Denkmodell, Syntax-Spickzettel, den typischen Fehlgriffen von Rust-/Swift-/Kotlin-geprägten
    Modellen, Prüfkommandos, Navigation in die Referenz).

- (Fund der Streams-Runde, 2026-09-20) Ein in TorbScript geschriebener `shared type` kann sich aus einer Methode mit
  `self` nicht ändern; asynchrone Methoden hatten `self`, also musste jeder Zustand in Closures mit gefangenen
  `var`-Bindungen versteckt werden (`Pulling`, `Staged`, `Buffered`).
  - **Entschieden (ich, Veto möglich) - wird gelöst:** "Ein `var self` kann nicht über ein `await` offen bleiben" gilt
    nur für Werte (exklusiver Zugriff, endet mit dem Aufruf). Bei einem Shared-Objekt ist `var` eine Berechtigung,
    keine Exklusivität - das Konzept-Beispiel hat heute schon zwei `var`-Pfade auf dasselbe Objekt. Also: eine
    `var self`-Methode darf einen `Task` liefern, wenn der Typ/Trait `shared` ist (bei Werten ein Fehler mit eigener
    Meldung). `Source.next(var self)`, `Sink.add(var self, item)`, `finish(var self)` - spiegelt `Iterator.next(var
    self)`; eine Quelle, aus der gelesen wird, steht in einer `var`-Bindung, `const` ist die Nur-Lese-Sicht wie bei
    jedem Shared-Objekt. Die Closure-Kisten werden gewöhnliche Shared-Typen mit `var`-Feldern.
  - **Erledigt:** Die positive Hälfte konnte der Checker schon (geprüft: `var self` + `Task`, zwei `next()`
    hintereinander, eine `var`-Quelle an eine awaitende Funktion, ein `var`-Feld über ein `await`, eine Closure mit
    `var`-Parameter - Tests in `compiler/tests/program-shape.test.trb`); die umgekehrte Richtung war still und ist jetzt
    "`take` changes `self` and answers a `Task`, and `Bag` is a value" bzw. "... `Counter` is not a `shared trait`"
    (`signature.trb`, greift auch bei Trait-Anforderungen und `native`-Deklarationen). `Source.next(var self)`,
    `Sink.add`/`finish(var self)`, `Buffered.flush(var self)`; **Lesen nimmt `var self`, Verpacken `self`** (ein
    Temporary ist kein `var`-Pfad, sonst wäre keine Pipeline mehr ein Ausdruck) - eine gelesene Pipeline bekommt also
    einen Namen, wie `var cursor = iterator()`. `Pulling`/`Pushing` bleiben die Closure-Notausgänge, alles andere
    (`Iterating`, `Stepping`, `Remapped`, `Checked`, `Staged`, `Produced`, `Buffered`) sind gewöhnliche Shared-Typen mit
    `var`-Feldern; `Response` ist ein `shared type` geworden. Dazu der offene Punkt 5: `extend<Target> Target with
    From<Never>` in `std/core`, also geht `?` auf einem `Result<Value, Never>`. Entscheidungen als Lücken 49 und 50 in
    `docs/TYPECHECKER.md`, Konzept und `docs/STREAMS.md` nachgezogen.

- (Chat, 2026-09-20) `...items: Item` ist eine `List<Item>` - wäre `Array<Item, count>`, ein Tupel oder ein (lazy)
  `Iterable` schlauer?
  - **Antwort / entschieden: es bleibt `List<Item>`.** `Array<Item, count>` macht jede variadische Funktion generisch
    über die Anzahl (eine Instanz pro Aufruflänge, nicht als Funktionswert oder Trait-Member nutzbar) und kann kein
    Spread (`...someSet` hat keine statische Länge). Ein Tupel bräuchte variadische Generics. Ein lazy `Iterable`
    bricht "Argumente sind vor dem Aufruf ausgewertet" (ein gespreizter `map`-Ausdruck mit Seiteneffekten liefe im
    Rumpf, vielleicht zweimal, vielleicht nie) und nimmt dem Rumpf `length()`, Index und zweites Durchlaufen. C# hat
    `params Span<T>`/`IEnumerable<T>` nur wegen der Heap-Kosten nachgerüstet - die hat TorbScript so nicht: Listen sind
    Werte mit Copy-on-Write, `f(...someList)` teilt den Speicher in O(1), `List.of` gibt `items` ohne Kopie zurück.
    Dass eine Argumentliste aus Literalen, die den Aufruf nicht überlebt, gar keinen Heap braucht, ist eine
    unsichtbare Back-End-Optimierung (Kandidat für 5.14), keine Frage des Typs.

- (Chat, 2026-09-20) `ArrayList` hat 29 `native fn` - sollte sie nicht auf `Array` aufbauen? Müssen alle Collections nativ sein?
  - **Antwort / entschieden:** Nein, müssen sie nicht, und sie bleiben es nicht. `Array<Item, Size>` taugt nicht als
    Basis (`Size` ist eine Compile-Zeit-Konstante, eine wachsende Liste braucht Speicher mit Laufzeitgröße). Der
    richtige Kern ist EIN nativer Typ `Buffer<Item>`: zusammenhängender Speicher mit Kapazität und einem
    initialisierten Präfix (`withCapacity`, `capacity`, `length`, `get`, `set`, `append`, `removeLast`, `swap`,
    `truncate`, `grown`) - so sieht TorbScript nie uninitialisierten Speicher, und Retain/Release der Elemente
    bleibt an einer Stelle. Darauf in TorbScript: `ArrayList` (`insert`, `removeAt`, `replace`, `reverse`, `sort`,
    `slice`, `iterator`, `from`, …), `HashMap`/`HashSet` (offene Adressierung über Buffers), die `Trie*`-Typen
    (Knoten als ADT). `ArrayQueue` und `ArrayStack` sind heute schon TorbScript. Heute: 7 native Typen, ~66 `native fn`.
  - **Zeitpunkt: nach dem Fixpunkt**, als Teil des zugesagten Schritts "Manifest-Audit → Kernel". Vorher nicht: die
    C-Runtime-Versionen (`list.c`, `map.c`) sind geschrieben und getestet, und der Compiler soll sich nicht zum ersten
    Mal mit unerprobten Collections selbst übersetzen. `iterator` wandert schon jetzt (5.7) nach TorbScript.
  - **Für JS/PHP:** genau dafür ist `native fn` mit TorbScript-Rumpf als Rückfall gedacht - portabel läuft der
    TorbScript-Rumpf über `Buffer`, ein Back-End darf `ArrayList` auf das JS-Array und `HashMap` auf `Map` abbilden.

- (Chat, 2026-09-20) `const nums: Array<Int, 4> = [1, 2, 3, 4]`, `Array.from [1, 2, 3, 4]`, `Array [1, 2, 3, 4]`; und
  ein Iterable soll dabei nicht das ganze Iterator-Protokoll durchlaufen müssen.
  - **Fund dabei:** der Checker nimmt heute JEDEN erwarteten Collection-Typ ungeprüft an - `const wrong: Array<Int, 4>
    = [1, 2, 3]` und `const names: Set<String> = ["a"]` prüfen ohne Meldung, und das Back-End erfährt nicht, wie das
    Literal zu bauen ist.
  - **Entschieden - wird gelöst (Agent läuft):** Ein Listen-Literal passt sich an (1) `List<Item>`/keine Erwartung:
    Standardliste wie heute; (2) `Array<Item, Size>`: wird direkt inline gebaut, die Anzahl ist statisch und muss
    `Size` sein ("`Array<Int, 4>` has 4 items, and this literal has 3"), Spread nur von Arrays bekannter Größe;
    (3) jeden Typ, der `From<Iterable<Item>>` ist (`ArrayList`, `ArrayQueue`, eigene Typen) - dasselbe Protokoll wie
    `.to<Target>()`, kein neues; (4) sonst ein Fehler. Was das Literal wurde, steht in den Tabellen fürs Back-End.
  - `const nums = Array.of(1, 2, 3, 4)` kommt dazu: die Zahl der Argumente löst `Size` (Sonderregel für `Array.of`,
    die Sprache bekommt keinen Weg, eine variadische Anzahl mit einem const-Parameter zu verknüpfen).
    `Array [1, 2, 3, 4]` geht nicht: das parst schon als Index-Ausdruck, und ein Kommando-Argument darf nicht mit `[`
    beginnen. `Array.from(iterable)` bleibt die Laufzeitform mit `?`-Ergebnis (Anzahl erst dann bekannt).
  - **Abkürzen des Iterator-Protokolls - kein Sprachfeature nötig:** für Literale baut der Compiler direkt (kein
    Iterator, bei `Array` nicht einmal eine Liste); für Werte kürzt `from` selbst ab: ein Typ-Pattern auf den
    Trait-Wert (`ArrayList` → Speicher teilen, O(1) dank Copy-on-Write), `Length` zum Vorbelegen. Rust macht dasselbe
    mit `size_hint` und Spezialisierung.

- (Chat, 2026-09-20) Regel: wenn am Aufruf nicht klar ist, was ein Argument bedeutet, wird es benannt - vor allem
  `true`/`false`/`None` (`listEntries entries, "ArrayList", hasCapacity: false`).
  - **Entschieden - so verstehe ich sie:** Ein Literal hat keinen eigenen Namen. `entries` sagt selbst, was es ist,
    `false` nicht - dort ist das Label die einzige Dokumentation (die "Boolean Trap"). Mechanisch prüfbar formuliert:
    **`true`, `false` oder `None` an einen Parameter, der als `Bool` bzw. optional DEKLARIERT ist, bekommt das Label.**
    Zwei Ausnahmen: (1) der Aufruf hat nur dieses eine Argument (`setEnabled(true)`, `assert(false)` - der
    Funktionsname sagt es); (2) das Literal ist die *Daten* und keine Option - genau dann, wenn der deklarierte
    Parametertyp ein Typparameter ist (`flags.set key, true`, `Some(true)`, `list.add(None)`); variadische Positionen
    lassen sich ohnehin nicht benennen. Ohne (2) träfe die Regel 277 Stellen in `compiler/src`, von denen die meisten
    `map.set key, true` sind - dort wäre `value: true` nur Lärm.
  - Folge für APIs: benannte Argumente stehen hinter den positionalen, also stehen Optionen in der Signatur hinten.
  - Zahlen-Literale: dieselbe Überlegung (`connect("localhost", timeout: 10)`), aber Ermessenssache
    (`Point(1, 2)`, `take(5)`) - als Stilregel, nicht als Lint-Fehler. Sag Bescheid, wenn du sie härter willst.
  - **Umsetzung:** Regel steht ab sofort in `compiler/CONTRIBUTING.md` (gilt für neuen Code). Durchsetzen kann das nur
    ein Werkzeug mit Typinformation (deklarierte Parametertypen, `argumentTargets` des Checkers): erste Regel von
    `torb lint --fix`, vorgezogen als kleiner Compiler-Auftrag, sobald 5.7 und 5.8 gemergt sind; stellt den Bestand um
    und kommt dann in CONCEPT. Nebenbei: `hasCapacity` als Parametername widerspricht der neuen Bool-Namensregel
    (Adjektiv statt `is`/`has`) - wird mit den Compiler-internen Namen nach dem Fixpunkt bereinigt.

- (Chat, 2026-09-20) Syntax-Highlighting verbessern: eigene Farbe für Generics, fehlende Keywords (`shared`), Felder ≠
  Locals ≠ Parameter (auch im Rumpf), `var` gegenüber `const` kursiv/unterstrichen statt farbig, Cases ≠ Typen,
  Methoden ≠ Funktionen, Kommando- und Aufrufstil gleich, Namen am Gebrauch wie an der Definition; Palette wie C# in
  Visual Studio / VS Code dunkel.
  - **Wird gelöst (Sonnet-Agent läuft).** Eine TextMate-Grammatik kann nicht wissen, ob ein Name im Rumpf Parameter,
    Local, Feld, `var` oder `const` ist - das braucht **semantische Tokens**. Bis zum LSP (Meilenstein 8) liefert sie
    ein neues Stage-0-Kommando `torb highlight --stdin` (Rust-Parser + kleiner Scope-Auflöser, JSON), die Extension
    ruft es als `DocumentSemanticTokensProvider` auf; fehlt das Binary, bleibt die TextMate-Färbung. Das LSP ersetzt
    es später hinter demselben Protokoll. `var` bekommt den eigenen Modifier `mutable` → unterstrichen (eine Regel,
    leicht auf kursiv umzustellen). Farben als Defaults der Extension in C#/Dark+-Anmutung, sprachgebunden (`:trb`),
    ohne dass du Settings anfassen musst. TextMate-Grammatik und Markdown-Vorschau bekommen die fehlenden Keywords
    (aus dem Lexer abgeleitet), `void`, `Fail`, Generics, `.Case`.

- (Fund des Streams-Nachzugs, 2026-09-20) "Eine Nur-Lese-Sicht auf ein Shared-Objekt lässt sich nicht wieder
  erweitern" wird nur an zwei von vier Stellen durchgesetzt (nicht bei Trait-Werten eines `shared trait`, nicht bei
  generierten Konstruktoren) - und das Umwickeln einer Quelle mit `self` stützte sich genau darauf.
  - **Entschieden (ich, Veto möglich) - wird gelöst:** kein neues Sprachmittel für "übergeben". Ein frisch erzeugtes
    Shared-Objekt als Zwischenergebnis gilt als `var`-Pfad: die Temporary-Regel schützt Werte (die Änderung ginge
    verloren), bei einem Objekt mit Identität geht nichts verloren, und auf ein Temporary hat niemand sonst eine
    Sicht. Damit nimmt auch das Umwickeln (`map`, `through`, `checked`, …) `var self` - eine Regel: wer aus einem
    Stream liest, jetzt oder später, braucht die Berechtigung; Pipelines bleiben ein Ausdruck; eine `const`-Quelle lässt
    sich nicht mehr verbrauchen; die Regel wird an allen vier Stellen durchgesetzt. `var writable = view()` bleibt
    legal (ein Aufrufergebnis ist kein Pfad; die Nur-Lese-Sicht ist ein Pfad-Versprechen, kein Typ).
  - **Erledigt:** Ein Temporary mit Identität ist ein `var`-Pfad (`problemOfRoot` in `place.trb`), ein Temporary-Wert
    behält seinen Fehler; die beiden `isSharedType`-Helfer sind einer geworden (`declaration.trb`, jetzt präzise: ein
    `shared type`, ein Trait-Wert, dessen Traits alle `shared` sind, ein Typparameter mit einer `shared`-Bound - und
    `false` für einen nackten Parameter, weil die Sprache keine Bound "shared" hat). Gap 20 greift jetzt an allen vier
    Stellen: Bindung, Feld (auch durch den generierten Konstruktor und innerhalb des Typs selbst, `requireSharedFields`),
    Argument und Trait-Wert eines `shared trait`. Die Umwickel-Member nehmen `var self` (`through`, `map`, `filter`,
    `then`, `mapFailure`, `checked`, `buffered`, `file.chunks`, `file.lines`), Pipelines bleiben ein Ausdruck. Das
    Einzige, was im Repository auf einem Loch stand, war `extend Body with From<Source<...>>` - `From.from` kann keinen
    `var`-Parameter deklarieren, also heißt es jetzt `Body.of(var source)`. `var writable = view()` bleibt legal und
    steht als Begründung in `docs/TYPECHECKER.md` (Lücke 52, mit "es gibt keine const-Typen"); Tests in
    `compiler/tests/places.test.trb`, Konzept und `docs/STREAMS.md` nachgezogen.
  - **Erledigt (Phase 1, gemergt `539f3c1`):** `docs/`-Baum (42 Seiten, 15 Ordner, 80 geprüfte Snippets), generierte
    Indexe, Frontmatter-Schema (`title`, `summary`, `kind`, `status` Pflicht), Fence-Marker (`trb`, `trb check`,
    `trb fragment`, `trb error` mit `// error:`-Kommentar, `trb skip <Grund>`), `torb docs check|index|skill|bundle`
    in TorbScript (64 Tests, Gates), Skill-Pipeline (`SKILL.md` 323 Zeilen + `reference/`), Recherche mit Quellen in
    `docs/contributing/research.md`, Inventar: ~170 Seiten in 13 Paketen.
  - **Meine Entscheidungen zu den offenen Punkten:** strenges YAML-Quoting bleibt; Snippets werden geprüft statt
    inkludiert; neun `kind`-Werte bleiben; der generierte Skill wird nach Phase 2 nach `.claude/skills/torbscript/`
    committet (mit Gate "ist aktuell"), damit ihn auch die Agents in diesem Repo benutzen.
  - **Phase 2 läuft:** Welle 1 = vier Sonnet-Schreiber (Pakete 2-5: Syntax/Werte, Funktionen, Typen, Traits/Generics),
    jeder im eigenen Worktree, Gates `docs check` + `docs index --check`.
  - **Zwei Compiler-Funde der Doku-Arbeit:** (1) der Dead-Change-Fehler feuert nicht bei `var first = counters[0]`
    + `first.increment()` (Konzept: Compile-Fehler) → geht an den Checker. (2) `default => …` in einem `match` bindet
    still und macht es vollständig. **Frage an dich:** soll eine nie benutzte Bindung in einem Catch-all-Arm ein
    Fehler sein ("write `_`")? Passt zur Dead-Change-Philosophie; ich bin dafür.

- (Stand, 2026-09-20 abends) Doku Phase 2, Welle 1 ist gemergt: **96 Seiten, 17 Ordner, 234 geprüfte Snippets**
  (Syntax, Werte und Typen, Funktionen, Typen, Traits und Generics). Welle 2 läuft (Pattern Matching/Fehler,
  Collections/Nebenläufigkeit/Streams, Module/Reflection/Konfiguration/Ausführung, Standardbibliothek).
  - **Die Doku-Schreiber sind ein Konformitätstest für den Checker geworden.** Weil jede Behauptung als geprüftes
    Snippet belegt werden muss, haben sie Stellen gefunden, an denen der Checker annimmt, was die Sprache verbietet:
    eine Liste, wo ein `Int` erwartet wird; ein Trait-Wert, wo ein konkreter Typ erwartet wird; `swap(a, a)` ohne
    Exklusivitäts-Fehler; `==`/`<` und `"{value}"` ohne `Equals`/`Compare`/`Show`-Prüfung bei ungebundenen
    Typparametern; `HashMap<Float, String>` trotz `Key: Hash`; "This is a bug of the compiler" bei fehlendem Member
    auf einem Trait-Wert; statische Funktion über eine Instanz aufrufbar; Feld und Methode gleichen Namens;
    `const (a, b) = …` aus dem Konzept prüft nicht; kleinere (Alias-Namen in Meldungen, `Array`-Index außerhalb,
    `extend` eines Literal-Typs, Default an `fn`-Typparametern, `NaN`/`nan`).
  - **Wird gelöst:** "Checker-Konformitätsrunde 1" (Opus-Agent) reproduziert jede Stelle einzeln, behebt sie mit
    Tests und exakten Meldungen und meldet, welche Doku-Sätze danach veraltet sind.

- (Fund aus 5.10 und der Streams-Runde, 2026-09-20) Ein Member mit EIGENEN Typparametern kann nie in einer
  Witness-Tabelle stehen: `Decoder.record<Output>`, `SequenceDecoder.next<Item>`, `RecordDecoder.field<Value>`,
  `Stage.onto<Final>`. Der Checker nimmt den Aufruf auf einem Trait-Wert an, das native Back-End kann ihn nie bauen.
  - **Entschieden (ich, Veto möglich):** kein zweites Übersetzungsmodell (geboxte Generics) - ein generischer Member
    ist **nicht objekt-sicher** und auf einem Trait-Wert ein Compile-Fehler; die std wechselt an diesen Stellen auf
    statischen Dispatch über gebundene Generics, wie serde: `fn decode<Chosen: Decoder>(var decoder: Chosen)`,
    `fn encode<Chosen: Encoder>(self, var encoder: Chosen)`, `Json.encode<Value: Encode>(value: Value)`,
    `through<Chosen: Stage<Item, Output>>(stage: Chosen)`; ein Format-Decoder implementiert alle vier Decoder-Traits
    in einem Typ, und die Closures von `record`/`sequence`/`map` bekommen `var fields: Self` statt eines Trait-Werts
    (ersetzt assoziierte Typen, die die Sprache nicht hat). Preis: keine heterogene `List<Encode>`.
  - **Zeitpunkt:** eigener Auftrag "Encoding auf statischen Dispatch" (Checker-Regel + `std/encoding`, `std/json`,
    `std/stream`/`std/iteration` `through`, Doku) nach 5.7 - der Compiler selbst benutzt weder `Encode` noch `Decode`,
    es liegt also nicht auf dem Weg zum Fixpunkt.
- (5.10, 2026-09-20) Format-Entscheidungen, die ich übernehme: `Void` zeigt sich als `void`; ein Tupel zeigt sich OHNE
  Labels (`(1, 9)`), weil Labels nicht zum Typ gehören - CONCEPT ("with the labels where there are any") wird
  angepasst; eine Escape-Tabelle in Runtime, Stage 0 und Dump; Floats nach dem kürzesten Round-Trip auf beiden Seiten.
  Offen: `Show` eines Funktionswerts - CONCEPT sagt "its type"; ich lege fest: die Quellschreibweise des Typs,
  `(Int64) => Int64`, Umsetzung mit 5.14 (Stage 0 druckt heute `<function>`).

- (Stand, 2026-09-21) **Doku Phase 2 ist komplett gemergt: 217 Seiten, 24 Ordner, 595 geprüfte Snippets**, `docs check`
  und `docs index --check` grün. Der **Agent Skill** ist erzeugt und committet: `.claude/skills/torbscript/`
  (`SKILL.md` 323 Zeilen + `reference/` mit 195 Seiten und generiertem Index; geplante und Contributing-Seiten
  bleiben draußen). Neu erzeugen: `torb run ../compiler docs skill ../docs ../build/skill/torbscript`, dann kopieren.
  - **Offen (Doku-Pflegerunde, nach Konformitätsrunde + 5.7):** Sätze, die durch Compiler-Fortschritt veralten -
    z. B. "`torb build` kann kein `print`" (seit 5.10 falsch), die "wird noch nicht gemeldet"-Hinweise, die die
    Konformitätsrunde schließt; dazu Snippets in eingerückten Fences, die das Werkzeug bisher nicht sah. Danach Skill
    neu erzeugen.
  - Weitere Funde der Schreiber (Welle 3): `project.trb` wird statisch gelesen statt ausgewertet (`authors`,
    `registry`, `build { target }` werden ignoriert), `project.lock.trb` und die Registry existieren nicht (geplant),
    Stage 0 beendet einen Panic mit Exit-Code 1 statt 101, `Bool` hat kein `Compare`.

- (5.7 Runde 3, 2026-09-21) Gemergt: `finish`-Lookup, `a[key]` lesen, `for` über Collections, Ranges als Wert,
  `collectionLiterals` gelesen - **15257 von 17250 Deklarationen gelowert (88 %)**, kein interner Fehler.
  - **Fund:** die Monomorphisierung terminiert nicht, sobald `Range.iterator` TorbScript ist: `Iterable.indexed` ist
    `Zipped(0.., self)`; die Tabelle von `Iterable<(Int, Item)>` enthält wieder `indexed`, das `Zipped<Int, (Int, Item)>`
    braucht, usw. (polymorphe Rekursion durch eifrig gebaute Witness-Tabellen).
  - **Entschieden (ich):** wie Rust (`enumerate` ist `where Self: Sized` und steht nicht in der vtable): ein
    **Default-Member, den keine Implementierung im Programm überschreibt, ist kein Tabellen-Slot**; er wird statisch
    mit `Self` = Trait-Typ instanziiert (ein Default erreicht `self` nur über die Member des Traits - das Verhalten
    ist identisch). Überschriebene Defaults bleiben Slots. Dazu eine Tiefengrenze als Notbremse mit sauberer Meldung
    statt Hänger. Nebeneffekt: die Instanzzahl pro Elementtyp fällt stark.
  - **Präzisierung der Objekt-Sicherheits-Entscheidung von oben:** ein generischer DEFAULT-Member (`map<Output>`) ist
    auf einem Trait-Wert weiter aufrufbar (statisch auf dem gelöschten `Self`, so läuft es schon); nicht objekt-sicher
    ist nur ein generischer Member, der VERLANGT ist (`Decoder.record<Output>`, `Stage.onto<Final>`).
  - `Show` einer `Range` (entschieden): `extend<Value: Show> Range<Value> with Show`, druckt die Quellform `0..10`.
  - **Lücke, die ich schließe:** drei Runden lang wurde kein natives Gate-Programm für Collections geschrieben - die
    88 % sind gelowert, aber nie gelaufen. Runde 4 beginnt deshalb mit Gate-Programmen (inkl. Copy-on-Write-Beweis)
    für alles, was schon lowert, und behebt, was sie finden; danach Instanzmengen-Entscheidung, `ArrayList.from`,
    Map-/Set-Cursor, Index-Pfade, Varargs/Spread (dann fällt die `print`-Sonderbehandlung weg), Listen-Patterns.

- (Checker-Konformitätsrunde 1, 2026-09-21) Gemergt (`bc0a413`): 1330 Tests, `check ..` ohne Probleme, volle
  `cargo test` grün. Geschlossen: Trait-Wert ↔ konkreter Typ und Collection ↔ Skalar (eine Ursache: ein Trait-Typ
  galt als "sagt nichts"), Spread in nicht-variadische Parameter, Exklusivität für Top-Level-`var`, `==`/`<`/
  Interpolation/Operatoren fragen nach ihrem Trait (auch bei ungebundenen Typparametern), Bounds an Typparametern
  eines Typs, fehlender Member auf Trait-Wert, statische Funktion nur über den Typ, ein Member-Namensraum,
  Schatten durch impliziten Closure-Parameter (5 echte Mehrdeutigkeiten in der std gefunden), Destructuring auf
  oberster Ebene, `await()` und `?` nur wo sie etwas bedeuten, umbenannte Cases, `.Configure`-Blöcke sind lokal
  (das Konzept-Beispiel `server { s => s.database { … } }` prüft wieder), `extend` eines Literal-Typs,
  statischer `Array`-Index außerhalb, Default an `fn`-Typparametern, der Canon-Widerspruch im Doku-Werkzeug,
  eingerückte Code-Fences. Neue Entscheidungen: Gaps 55-59 in TYPECHECKER §9 (u. a. "eine Bound auf `Never` gilt").
  - **Nicht reproduziert / kein Fehler:** `swap(a, a)` mit Locals war schon ein Fehler; die `Encode`-Bound an
    Captures greift; `Decimal` bleibt typprüfbar (der Doku-Kommentar in `std/number` war falsch).
  - **Offen gelassen:** Alias-Namen in Meldungen (`EntityId (Int64)`); `?` bei INFERIERTEM Ergebnis; welche Closures
    ein Aufrufer als Task ausführt (Gap 58, braucht ein Signatur-Mittel - Meilenstein 7); `nan` statt `NaN` (Runtime).
  - **Folge:** `docs check` ist auf `master` vorübergehend ROT (37 Probleme: 29 nie geprüfte Snippets in
    eingerückten Fences + veraltete "wird noch nicht gemeldet"-Stellen). Eine Doku-Pflegerunde (Sonnet) behebt das,
    aktualisiert die Aussagen zum nativen Back-End und erzeugt den Skill neu.

- (5.7 Runde 4, 2026-09-21) Gemergt (`8854bc4`), alles grün (1332 Tests, volle `cargo test`). **Die Collections
  laufen jetzt nativ:** drei Gate-Programme (`collections.trb`, `ranges.trb`, `collection-index.trb`), byte-gleich
  mit Stage 0, 0 lebende Blöcke, Copy-on-Write über Local, Feld und `var`-Parameter bewiesen. Das Laufen hat sechs
  Fehler gefunden, die das Lowern allein nie gezeigt hätte (u. a. Witness-Thunks liehen alles - doppelte Freigabe bei
  `for x in list`; Closure-Parameter dürfen nie `Owned` sein; der Out-Parameter von `list.get` ist nur auf einem Pfad
  belegt; keine Collection war druckbar). Vorher ließ sich NICHTS mit Collections bauen (alles-oder-nichts über die
  Tabelle von `List<Item>`) - deshalb hatten drei Runden kein Gate-Programm.
  - **Instanzmenge begrenzt (meine Entscheidung, umgesetzt):** nicht überschriebene Defaults sind keine
    Tabellen-Slots. Stolperdraht 208 → **78 Deklarationen**, Repository 17250 → **9182 Deklarationen, 90 % gelowert**,
    `Range.iterator` und `ArrayList.from` sind TorbScript, Supertrait-Tabellen (`nested`) gebaut, Tiefengrenze 10 als
    Notbremse mit sauberer Meldung. `Show` einer `Range` druckt die Quellform.
  - **Neuer Kompass: der Compiler selbst.** `torb ir --statistics ../compiler`: **7471 von 7954 (93 %)**. Es fehlen:
    `var`-Argument/-Receiver/Zuweisung durch `a[key]` (178), `TrieMap.iterator` und damit alle Map-/Set-Literale
    (130), quotierte Ausdrücke = `assert` in den Tests (40), nicht faltbare Konstanten (35), `String.chars`/`bytes`/
    `from` (35), `sort`/`slice` auf Trait-Werten (29), Listen-Patterns (6), CLI-Natives (`File.createDirectory`,
    `Process.run`, `milliseconds`), 1 interner Fehler (Closure gibt geliehenen Parameter zurück).
  - **Entschieden (ich):** Index-Pfade - das Lowering läuft den Ziel-Ausdruck im Gleichschritt mit den Pfad-Schritten
    ab, jeder Schlüssel wird genau einmal, in Quellreihenfolge und VOR Beginn des Zugriffs ausgewertet (Swift-Modell);
    keine Checker-Änderung. `language.trb` ist als Gate von 5.7 abgelöst (es ist ein Stage-0-Skript, das der Checker
    an 11 Stellen zu Recht ablehnt) - ob es repariert oder ersetzt wird, gehört zu 5.14.
  - **Läuft (zwei Opus-Agents parallel):** "Collection-Kern" (Index-Pfade, Map-/Set-Cursor, `sort`/`slice`,
    Varargs/Spread → `print`-Sonderfall weg) und "langer Schwanz" (interner Fehler, Konstanten, `String.chars`,
    Listen-Patterns, CLI-Natives 5.12, Treiber 5.13, quotierte Ausdrücke 5.11).

- (Patterns und Schreibweise, 2026-09-21) **Entschieden (Nutzer):** Die Schreibweise wird Sprachregel. Typen, Traits,
  Cases und Typparameter beginnen mit einem Großbuchstaben; Funktionen, Methoden, Felder, Parameter, Locals und
  Konstanten beginnen klein (kein MACRO_CASE) - alles andere ist ein Compile-Fehler an der Deklaration. Damit ist die
  Pattern-Regel "groß = Case, klein = Bindung" keine Konvention mehr, sondern vom Compiler garantiert. Betroffen ist
  ohnehin nur EINE Stelle: der einzelne nackte Name ohne Klammern und ohne Punkt (`None`, `Active`); `Name(...)` und
  `.Name` sind an Klammer bzw. Punkt eindeutig, und einen nackten Typnamen gibt es in Patterns nicht (kein Typtest).
  - **Dazu (ich):** Eine ungenutzte Bindung in einem `match`-Arm wird ein Fehler ("schreib `_`", oder `_name`, wenn
    der Name dokumentieren soll) - das schließt `limit =>`, das still eine sichtbare Konstante überdeckt.
  - **Offen (Nutzer):** Bezeichner auf ASCII beschränken? Meine Empfehlung: ja, ohne Mathe-Whitelist (Analyse im
    Chat: Homoglyphen/UTS 39, Normalisierung, zwei Lexer + C-Runtime müssten dieselben Unicode-Tabellen tragen -
    `torb_char_is_letter` ist oberhalb von ASCII heute nur genähert, das wäre eine Fixpunkt-Abweichung; `Δt` wäre nach
    der Schreibregel ein Typ). Öffnen geht später ohne Bruch, Schließen nicht.
  - Umsetzung: kleine Checker-/Lexer-Runde nach den beiden Back-End-Merges.
  - **Entschieden (Nutzer, 2026-09-21):** Bezeichner sind ASCII: `[A-Za-z_][A-Za-z0-9_]*`, keine Mathe-Whitelist.
    "Groß" heißt `A`-`Z`; ein Name, der mit `_` beginnt, zählt als klein. Strings, Chars, Kommentare und Doku bleiben
    volles Unicode. **Wird gelöst:** eine Runde (Opus-Agent) - beide Lexer, Schreibregel im Checker, Fehler für die
    ungenutzte Pattern-Bindung samt `torb canon --rule unused-bindings` für die Repo-Korrektur, CONCEPT, Doku, Skill,
    VS-Code-Grammatik.

- (Extensions, 2026-09-21) **Entschieden (Nutzer):** Nichts Fremdes ist mehr implizit sichtbar. Grundsatz: ein Member
  ist überall sichtbar, wenn das Paket des TYPS ihn angebracht hat (`type Circle with Shape`, `extend` auf den eigenen
  Typ); sonst nennt die Datei beim Namen, woher er kommt.
  - Einzelne Extension-Member per Pfad, dieselbe Form wie Cases: `use String.shout, String.slug from "acme/text"`,
    `use Int64.seconds from "std/time"`. Konflikt: `as` (`use String.shout as yell from "..."` → `"x".yell()`); die
    Namespace-Form `text.shout(value)` entfällt (war verkapptes UFCS).
  - Ein Bündel ist ein Trait (Rust-Modell): die Member eines Traits, den das Paket des TRAITS auf einen fremden Typ
    legt (`extend String with Slug`, auch Blanket-Implementierungen), sind dort sichtbar, wo der Trait sichtbar ist
    (`use Slug from "acme/slug"`, Prelude, Bound `Item: Slug`, Trait-Typ). Milder als Rust: was der Typ selbst
    mitbringt, braucht keinen Import.
  - `use "modul"` ohne Namen entfällt (beim Import läuft nichts). Das Prelude re-exportiert namentlich
    (`public use Int64.seconds from "std/time"`).
  - Nur Sichtbarkeit von Namen; Kohärenz und Dispatch bleiben. **Wird gelöst:** Checker-Runde NACH der
    Schreibregel-Runde (gleiche Dateien).
  - **Erledigt (2026-09-22).** Ein Member-Import ist ein eigener Eintrag im Modul-Scope (`MemberImport` in
    `scope.trb`, Schlüssel = Typ-Symbol + lokaler Name), der Fixpunkt bindet ihn wie jeden anderen Namen und das
    Prelude reicht ihn über `public use` weiter. Zwei Entscheidungen, die die Regel so nicht abdeckte:
    - **Das eigene Paket braucht keinen Import.** `extend Int64 { fn megabytes }` in `std/sandbox` in derselben
      Datei zu importieren wäre absurd, also gilt: sichtbar sind die Extensions des Pakets des TYPS **und** die des
      benutzenden Pakets. Alles andere wird benannt.
    - **Der erste Buchstabe entscheidet, ob ein Pfadsegment einen Case oder einen Member meint** (groß = Case).
      Sonst würde `use Option.Maybe from "std/core"` "kein Member `Maybe`" melden statt "kein Case `Maybe`".
    - `as` benennt auch einen Member des eigenen Pakets um - sonst hätte ein Konflikt innerhalb eines Pakets keinen
      Ausweg.
    - Ein `use`, das einen Member nennt, den das Paket nicht anbringt, wird an der `use`-Zeile gemeldet
      (`` `./text` adds no member `whisper` to `String` ``), nicht erst am Gebrauch.
    - Im Repository brach das genau **zwei** Stellen: `2.seconds()` (Prelude-Re-Export) und `16.megabytes()` in
      `examples/config-dsl` (`use Int64.megabytes from "std/sandbox"`). Die Trait-Hälfte brach nichts, weil jeder
      Trait, den die std auf einen fremden Typ legt (`Encode`, `Decode`, `Show`, `From`, `Into`), im Prelude steht.
    - Stage 0 akzeptiert die Form und setzt den `as`-Alias um (global - mehr geht in einem untypisierten Stage 0
      nicht, und es ist dokumentiert).

- (Schleifen und `?`-Operatoren, 2026-09-21)
  - **Zurückgestellt (Nutzer):** kein `for await`, kein Präfix-`await`. `while const Some(x) = source.next().await()?`
    bleibt; mittelfristig kommt eine Schleifenform für Sources (offene Frage dabei: wo bleibt das `?` sichtbar).
  - **Entschieden (Nutzer wünscht, ich halte es für sinnvoll):** `loop { ... }` für Endlosschleifen. Ohne `break` hat
    es den Typ `Never`, mit `break` ist es `Void`; kein `break value` (lässt sich später ohne Bruch ergänzen). Es
    ersetzt den Sonderfall im Checker, der heute das LITERAL `while true` als divergierend erkennt - damit ist
    "endet nie" eine Eigenschaft der Syntax und nicht einer Bedingung. Eine Schreibweise: `while true` wird ein Fehler
    ("schreib `loop`"), die 24 Stellen im Repository stellt `torb canon` um. `loop` ist als Name nirgends belegt.
  - **Meine Empfehlung zu `?`, `?.`, `??` als Traits (Antwort im Chat):** Kriterium "ein Operator ist genau dann ein
    Trait, wenn er ein Methodenaufruf ist". `??` IST `orElse` → Trait `OrElse<Value>` (billig, konsistent mit
    "Operatoren fragen nach ihrem Trait"). `?.` wählt je nach Ergebnistyp zwischen `map` und `flatMap` und bräuchte
    `Self<Output>` - das ist die Higher-Kinded-Form, die CONCEPT ausschließt → bleibt `Option`. `?` verlässt die
    umgebende Funktion, das kann keine Methode; Rusts `Try`-Trait ist seit 2016 instabil (Residual-Typen, Kollision
    mit Blanket-`From`) → bleibt `Option`/`Result`, kann später ohne Bruch geöffnet werden.
  - **Entschieden (Nutzer, 2026-09-21):** so wie empfohlen - `??` wird der Trait `OrElse<Value>`, `?.` bleibt
    `Option`, `?` bleibt `Option`/`Result`. **Wird gelöst:** zusammen mit `loop` und der Extension-Sichtbarkeit in der
    nächsten Checker-Runde (nach der Schreibregel-Runde).
  - **Erledigt (2026-09-22), `loop`:** `StatementKind.Loop(body)` in beiden Parsern, Stage-0-Interpreter, Checker
    (`while true` ist der Fehler "A loop that never ends is written `loop`"), IR-Lowering (ein Block, der auf sich
    selbst zurückspringt - ohne Bedingung und damit eine Instruktion weniger als `while true`), `torb highlight`,
    TextMate-Grammatik, VS-Code-Erweiterung. `torb canon --rule loops` ist da und im Gate von CONTRIBUTING; der
    Sweep sind **17 Stellen in 11 Dateien** (die 24 aus der Zählung enthielten Test-Strings und Kommentare) und
    liegt als letzter, rein maschineller Commit. `loop` war als Name doch belegt: zwei Locals in
    `compiler/tests/ir.test.trb` und `quotations.test.trb`, umbenannt zu `again`/`repeated`.
  - **Erledigt (2026-09-22), `??`:** `public trait OrElse<Value>` in `std/core/src/operators.trb`, im Prelude, von
    `Option` und `Result` über ihr bestehendes `orElse` getragen. Der Checker fragt `traitArgumentsOf` und meldet
    "`X` does not implement `OrElse`, so `a ?? b` has no meaning for it"; die Auflösung ist ein `recordTraitCall`
    wie bei `==` und `<`, das Lowering von `??` ändert sich dadurch nicht.

- (std/ecs und die Engine-Bibliotheken, 2026-09-21) **Wunsch (Nutzer), eingeplant für Meilenstein 10:** `std/ecs` auf
  Unity-/Godot-Niveau, mit einer trb-basierten Szenen-DSL (wie Prefabs/`.tscn`), losgelöst von der Grafik (Web Canvas,
  DirectX, Metal, OpenGL, ... sind austauschbare Back-Ends). Teile davon werden eigene std-Pakete, sodass die std am
  Ende eine vollständige Game-Engine tragen kann. Das ECS in `examples/game-engine` ist dafür zu statisch (eine
  geschlossene `Component`-Summe, `Map<ComponentKind, Component>` pro Entity).
  - **Zweck daneben:** Härtetest für die Sprache. Erwartete Lücken (meine Analyse, Details im Chat): (1) offene
    Komponentenmenge ohne Reflection - Typschlüssel und typisierte Spalten ohne Downcast; (2) typisierte Queries über
    mehrere Komponenten - braucht variadische Typparameter oder eine andere Antwort, Makros gibt es nicht; (3)
    In-place-Mutation beim Iterieren (`var`-Closure-Parameter über dichten Spalten) und Exklusivität zweier Queries;
    (4) parallele Systeme gegen "Werte über Task-Grenzen, Shared nie"; (5) Szenen-DSL = Receiver-Skript in der Sandbox,
    aber SPEICHERN braucht trb als Datenformat (Encoder) und eine Registry Name → Decoder; (6) FFI für Grafik-APIs;
    (7) Layout/Performance: `Float32`, SoA auf `Buffer<Item>`, keine Refcount-Prüfung in heißen Schleifen.
  - **Plan:** nach dem Fixpunkt ein Design-Dokument `docs/ECS.md` (Recherche Bevy/flecs/Unity DOTS/Godot, wie bei
    STREAMS.md), dann Paketschnitt (`std/ecs`, `std/scene`, `std/geometry`, `std/input`, `std/asset`, `std/render`
    als abstrakte Schicht, ...). Die Sandbox-DSL hängt an der VM (7.x).
  - **Ergänzt (Nutzer, 2026-09-21):** dazu std-Pakete für Geometrie, Kollision, 2D, 3D, Matrizen, Transformationen,
    Animationen, Pfade usw. **Vorläufiger Schnitt (ich, wird im Design-Dokument festgelegt):** `std/linear`
    (`Vector2/3/4`, `Matrix3/4`, `Quaternion`, generisch über `Float32`/`Float64`), `std/geometry` (Formen 2D/3D:
    `Rectangle`, `Circle`, `Box`, `Sphere`, `Ray`, `Plane`, Schnitt- und Abstandstests), `std/transform`
    (`Transform2`/`Transform3`, Hierarchien, Koordinatenräume), `std/collision` (Broadphase, Narrowphase, Raycasts;
    Physik baut darauf auf), `std/path` (Bezier, Splines, Polylinien, Tessellierung - dieselben Pfade für Canvas/SVG
    und für Bewegung), `std/animation` (Easing, Keyframes, Tracks, Tweens, Zustandsautomat; interpoliert alles, was
    einen `Interpolate`-Trait trägt), `std/color`. Alle sind reine Werte-Bibliotheken ohne ECS- und ohne
    Grafik-Abhängigkeit; `std/ecs` und `std/render` benutzen sie, nicht umgekehrt. Sie prüfen vor allem Generics
    über Zahltypen, Operator-Traits, `Array<Item, Size>` und das Wert-Layout ohne Boxing.
  - **Entschieden (Nutzer, 2026-09-21):** `std/linear` und `std/geometry` sind die ersten Pakete direkt nach dem
    Fixpunkt (brauchen weder VM noch FFI), der Rest folgt dem Design-Dokument.
  - **Ganzzahl-Vektoren und -Geometrie (Nutzerwunsch, 2026-09-21; pixelbasiert/deterministisch) - Entschieden (ich):**
    EIN generischer Typ statt Zwillingstypen wie Godots `Vector2`/`Vector2i` (ohne Makros wären das von Hand
    gepflegte Kopien, und `Vector2Int` wäre eine Abkürzung im Namen): `type Vector2<Scalar = Float> { x: Scalar,
    y: Scalar }` - `Vector2` ist der Float-Vektor, `Vector2<Int>` der Pixel-Vektor. Die Methoden sind nach Bounds
    geschichtet (bedingtes `extend`): über `Numeric` (gibt es schon in `std/number`) alles, was ohne Wurzel und
    Winkel auskommt (`+ - *`, `dot`, `lengthSquared`, `scaled`, `min`/`max`, Manhattan-Länge, `Rectangle`-Schnitt,
    Raster/Tilemap); über einen NEUEN Trait `Real` (`squareRoot`, `sine`, ..., für `Float32`/`Float64`) der Rest
    (`length`, `normalized`, `rotated`, Kreise, Matrizen, Quaternionen). Übergänge nur ausdrücklich (`toFloat()`,
    `rounded()`, `floored()`), wie überall in der Sprache. **Der eigentliche Gewinn für Determinismus:** ein
    Festkomma-Typ (`Fixed`, Q-Format) trägt `Real` - dann läuft die GANZE Geometrie/Kollision bitgleich auf jeder
    Plattform und in jedem Back-End (Lockstep, Replays), was Floats wegen `sin`/`cos` der jeweiligen libm und wegen
    des JS-Back-Ends nie garantieren. Im Design-Dokument zu prüfen: Bound plus Default an einem Typparameter
    (`Scalar: Numeric = Float`), `From` zwischen `Vector2<Source>` und `Vector2<Target>` gegen die Blanket-Regel.
  - **Erledigt:** Design-Dokument `docs/ECS.md` (14 Abschnitte, Recherche Bevy/flecs/Unity DOTS/Godot/EnTT) plus
    `examples/ecs-probe` - prüft, läuft auf Stage 0, baut nativ, und beide Ausgaben sind gleich. Alle Gates grün
    (`check ..` 317 Dateien, `--statistics` 195885/195885 und 0 deferred, `canon --check` 0 von 389, `docs check`
    222 Seiten, `docs index --check` 24 Indizes).
    - **Die vier wichtigsten Entscheidungen.** (1) **Nichts wird typgelöscht.** Die Welt ist der EIGENE Typ des
      Programms mit einer `Column<Component>` je Komponententyp; `std/ecs` greift über `trait Store<Component>`
      darauf zu, und der Typschlüssel `componentKey<Component>()` ist nur ein NAME (Szenendatei, Inspector,
      Zugriffsmenge), nie ein Weg zum Speicher. Die offensichtliche Alternative (`Map<Key, AnyColumn>` plus
      Downcast) ist zu, und der Checker tut heute nur so, als wäre sie offen (siehe Lücke 5). (2) **Dichte Spalten
      mit Sparse-Index** (EnTTs Modell), keine Archetyp-Tabellen: eine Archetyp-Tabelle müsste Werte zwischen
      Tabellen verschieben, und der Code, der sie verschiebt, hat ihren Typ vergessen. (3) **Kein Command-Buffer.**
      Das Subjekt eines `for` wird einmal in ein Temporary ausgewertet, also läuft eine Query über eine KOPIE der
      Welt und Spawn/Despawn landen in der lebenden - im gebauten Binary nachgewiesen. Und weil eine Welt ein WERT
      ist, ist ein Snapshot eine Bindung und ein Rollback eine Zuweisung; das hat keines der recherchierten Systeme.
      (4) **Die Szene ist eine `.trb`-Datei** über ein Receiver-Skript (`Sandbox.load<Scene<World>>`, Verschachtelung
      IST die Eltern-Beziehung, `instance` mit Overrides wie Godots `.tscn`). Gespeichert wird als
      `attach Position(Vector2(...))` - ein Wert ist sein Konstruktoraufruf -, und ein Name findet seinen Decoder
      über eine Registry aus Closures, die dort geschrieben wurden, wo der Typ bekannt war: kein `TypeRegistry` wie
      Bevy, kein `ecs_meta` wie flecs, keine Reflection.
    - **Die schlimmsten Sprachlücken** (Abschnitt 11, nach Schmerz geordnet, jeweils mit Reproduktion):
      (1) **Zwei Implementierungen EINES Traits mit verschiedenen Argumenten an EINEM Typ sind nicht erreichbar.**
      Nur ein Member, dessen Trait-Argument im ERSTEN Parameter steht, löst auf; über den Ergebnistyp
      (`valueOf(self): Component?`) oder einen späteren Parameter nimmt die Auflösung stumm die erstdeklarierte
      Implementierung ("Expected `Option<String>`, found `Option<Int64>`"), und über eine Schranke
      `World: Store<First> & Store<Second>` prüft es grün, Stage 0 liefert zweimal denselben Wert und das Back-End
      meldet einen internen Fehler. Das ist Lücke 12 aus `docs/LINEAR.md` - dort eine Unbequemlichkeit, hier die
      tragende Wand: OHNE sie kann ein Programm gar nicht sagen "diese Welt hat eine Spalte `Position` UND eine
      Spalte `Velocity`". (2) **Keine variadischen Typparameter** (`fn tuples<...Components>()` sind fünf
      Parse-Fehler), deshalb `query2`/`query3`/`query4`; kleinste ehrliche Form: ein Pack, das NUR als Elementliste
      eines Tupeltyps und als Subjekt einer Schranke expandiert, ohne Indizierung und ohne Längenarithmetik.
      (3) **Ein Member einer Blanket-Implementierung wird an einem konkreten Typ nicht gefunden**
      ("`Game` has no member `doubled`"), auch ohne Generics am Member - deshalb ist heute JEDE Query eine freie
      Funktion und `world.query2<Position, Velocity>()` ist gar nicht schreibbar. (4) **Eine Closure kann keinen
      `var`-Parameter binden**: `{ var position => ... }` meldet "A binding needs a value"; der Parametertyp
      `(var value: Component) => Void` wird akzeptiert und ein benanntes `fn` funktioniert, aber das kann nicht
      capturen. (5) **Soundness-Loch: ein trait-typisierter Wert wird akzeptiert, wo ein Typparameter erwartet
      wird.** `fn bare<Value>(shape: Area): Value { shape }` prüft grün, Stage 0 druckt `Square(side: 3)`, der
      Back-End-Verifier meldet "`return` carries `Object(Area)`" - ein BENANNTER Typ lehnt dieselbe Zeile korrekt ab.
    - **Daneben gefunden:** `spawn` fehlt auf Stage 0 ganz, und der Checker AKZEPTIERT eine `var`-Bindung, die eine
      `spawn`-Closure captured, obwohl CONCEPT es verbietet; das Back-End lehnt "ein `var`-Argument über ein
      Top-Level-`var`" ab - parallele Systeme sind heute in keiner Form prüfbar (dieselbe Lücke wie datenparallele
      Schleifen bei `std/tensor`, eine Reparatur bedient beide). `Sandbox` fehlt auf Stage 0, also wartet die
      Lese-Hälfte von `std/scene` auf die VM (7.x). Stage 0 löst KEINEN Typ-Alias über einen Import auf
      ("`./ecs/world` does not declare `System`"), weshalb `examples/game-engine` dort gar nicht läuft. Ein lokales
      `type` kompiliert und läuft nativ, Stage 0 lehnt es ab. Ein `Array<Int, 4>`-Literal baut das C-Back-End nicht
      (Lücke 9 aus LINEAR). **Und ein kompiliertes Binary schreibt unter Windows CRLF, wo Stage 0 LF schreibt** -
      allgemein, nicht ECS-spezifisch, aber es macht einen Byte-Vergleich der beiden Stufen unter Windows unmöglich.
    - **Offene Fragen an dich** stehen in Abschnitt 14 von `docs/ECS.md` (nur Geschmack und Richtung): `query2/3/4`
      oder `query`/`pairs`/`triples`; soll `std/ecs` je eine eigene `World` mitbringen (heute unmöglich, und nach
      der Reparatur immer noch nicht richtig); `std/scene` eigenes Paket oder Modul; darf eine Szenendatei
      `std/linear` und `std/time` nennen; gehören Events ins Paket; ist `GlobalTransform2` eine Komponente oder ein
      Feld; und wem gehört die Frame-Schleife.

- (ML/KI in der std, 2026-09-21) **Wunsch (Nutzer), eingeplant für Meilenstein 10, gemeinsam mit den Engine-Paketen:**
  Training und Inferenz sollen mit der std möglich sein. **Entschieden (ich): kein eigener Turm, sondern dasselbe
  Fundament wie Geometrie und Engine, in vier Schichten:**
  - **Schicht 0 - Sprache/Runtime (gemeinsam):** `Buffer<Item>`-Kern (ohnehin nach dem Fixpunkt geplant), Trait `Real`
    (aus `std/linear`), `Float16`/`BFloat16`, FFI (dieselbe Lücke wie bei DirectX/Metal), datenparallele Schleifen
    über disjunkte Buffer-Abschnitte (dieselbe Lücke wie parallele ECS-Systeme), SIMD im C-Back-End.
  - **Schicht 1 - reine Werte:** `std/linear`, `std/geometry` (kleine feste Vektoren), `std/tensor` (n-dimensional,
    Broadcasting, Slices als Fenster, auf `Buffer<Item>`; Form zur Laufzeit geprüft, `Matrix<Scalar, Rows, Columns>`
    bleibt in `std/linear`), `std/random` (seedbar und TEILBAR, ein Wert wie JAX-Keys - reproduzierbares Training),
    `std/statistics`. Wert-Semantik passt: ein Tensor ist ein Wert, und eine Änderung über einen `var`-Pfad mit
    einem einzigen Besitzer geschieht ohne Kopie (das, wofür JAX "donation" braucht).
  - **Schicht 2 - Ableiten und Rechnen:** `std/gradient`. Vorwärtsmodus als `Dual<Scalar>` mit `Real` - damit ist
    JEDE über `Real` generische Funktion (auch Geometrie, Animation, Physik) ohne Zutun ableitbar. Rückwärtsmodus über
    QUOTIERTE AUSDRÜCKE (`gradient { x => ... }` bekommt den Ausdrucksbaum wie der Query-Provider) - das ist unser
    einziger Meta-Mechanismus und ersetzt Makros/Tracing. `std/gpu`: eine abstrakte Rechen- und Zeichen-Schicht nach
    dem WebGPU-Modell (bildet auf DirectX/Metal/Vulkan/WebGPU ab); derselbe Weg "quotierter Ausdruck → Kernel/Shader"
    dient `std/render` UND `std/tensor`.
  - **Schicht 3 - Anwendung:** `std/learning` (Schichten, Optimierer, Verluste, Trainingsschleife; Daten kommen als
    `Source<Batch, Failure>` - das Stream-Protokoll ist die Daten-Pipeline), Modellformate (safetensors, ONNX, GGUF)
    über die Encoding-Schicht, quantisierte Inferenz über Ganzzahl-/`Fixed`-Skalare. Daneben `std/ecs`, `std/render`,
    `std/collision`, `std/animation`.
  - **Reihenfolge nach dem Fixpunkt:** `std/linear` + `std/geometry` (bringen `Real`) → `Buffer<Item>` →
    `std/tensor` auf der CPU + `Dual` (alles reines TorbScript, ohne FFI) → FFI-Design → `std/gpu` → Rückwärtsmodus,
    `std/learning`, `std/render`, `std/ecs`. Zwei Design-Dokumente: `docs/ECS.md` und `docs/COMPUTE.md`
    (Tensor/Gradient/GPU; Recherche JAX, PyTorch, Burn, tinygrad, Mojo, Swift for TensorFlow).
  - **Neue Härtetests für die Sprache:** Rechnen mit Größenparametern (`Rows * 2`) - voraussichtlich nicht, daher
    Laufzeitform; Operator-Traits mit fremdem `Other`/`Output` (Tensor × Skalar); Buffer über 2^31 und
    speicherabgebildete Dateien; quotierte Ausdrücke mit Kontrollfluss; garantierte In-place-Änderung bei einem Besitzer.

- (Dokumentation AM Code, 2026-09-21) **Wunsch (Nutzer):** aller trb-Code im Repository (std, Compiler, Examples) ist
  sauber und simpel dokumentiert, für Menschen und Agents: was ein Konstrukt JETZT tut und wofür es da ist, simple
  Beispiele für die Anwendungsfälle, verwandte Konstrukte verlinkt, Pitfalls und offene Probleme am Code sichtbar.
  Keine Vergangenheit ("früher", "nicht mehr", Meilenstein-/Rundennummern).
  - **Entschieden (ich) - der Standard** (baut auf CONCEPT "Doc Comments" auf, kommt in CONTRIBUTING): erster Satz =
    was es tut; dann wofür es da ist. Feste Überschriften, sonst keine: `# Examples` (laufen als Tests), `# Errors`,
    `# Panics`, `# Pitfalls`, `# Open` (offenes Problem, im Präsens, ohne Plan-Nummern), `# Related` (Links
    `[Iterator]`, `[List.add]`, werden aufgelöst). Jede Datei beginnt mit einem Modul-Kommentar (wofür das Modul da
    ist, seine Hauptkonstrukte, wie sie zusammenhängen). Pflicht: jede `public`-Deklaration in std; im Compiler jede
    `public`-Deklaration und jede Datei; in Examples jede Datei und jedes gezeigte Konstrukt.
  - **Entschieden (ich) - das Gate:** `torb docs source <pfad>` prüft genau das (fehlender Kommentar, fremde
    Überschrift, Link löst nicht auf, Beispiel checkt nicht, Wörter der Vergangenheit) - ohne Gate verrottet es.
  - **Reihenfolge (wegen Merge-Konflikten mit den laufenden Code-Runden):** jetzt das Werkzeug + der Standard
    (berührt nur `compiler/src/documentation/`); dann Welle 1 `examples/` + `std/` (Sonnet-Schreiber, paketweise, Gate
    als Abnahme); Welle 2 `compiler/src` nach den Back-End-Merges und der nächsten Checker-Runde; Tests bekommen nur
    einen Datei-Kommentar. Umfang: 256 Dateien, rund 82.000 Zeilen (davon Compiler 54.000).

- (std/markdown, 2026-09-21) **Wunsch (Nutzer), eingeplant:** ein Markdown-Parser und -Writer in der std,
  mittelfristig. **Einordnung (ich):** Bedarf gibt es im eigenen Haus schon dreifach - Docblocks SIND Markdown
  (`torb doc`, Hover im Language Server, beides Meilenstein 8), und das Doku-Werkzeug hat heute einen eigenen,
  schmalen Leser (`compiler/src/documentation/markdown.trb`, 686 Zeilen: Front Matter, Überschriften, Zäune, Links).
  Deshalb VOR Meilenstein 8, nicht erst mit der std-Breite: `std/markdown` mit einem Dokumentbaum als Wert
  (Blöcke/Inlines als Cases), Parser nach CommonMark (die ~650 Beispiele der Spezifikation sind die Testsuite) plus
  Tabellen und Front Matter, Writer mit Round-Trip, HTML-Ausgabe als eigener Schritt. Es ist ein DOKUMENTformat,
  also eigene Traits neben `Encode`/`Decode` (so steht es in CONCEPT), und blockweise streambar über `Source`. Der
  Leser im Compiler wird danach durch das Paket ersetzt.

- (Langer Schwanz, 2026-09-21) **Erledigt, gemergt, Gates grün** (1343 Tests, 0 zurückgestellt, 91 Runtime-Tests, volle
  `cargo test` im Zweig): Compiler **7693 von 8112 (94 %), 0 interne Fehler** (vorher 93 %, 1). Nicht faltbare
  Konstanten werden am Leseort gelowert (keine Modul-Initialisierung, nie); `chars`/`bytes`/`String.from`/`slice`
  sind TorbScript über zwei Natives; Listen-Patterns; `.Fallible`-Natives mit mehreren Out-Parametern - vorher ließ
  sich KEIN Programm bauen, das eine Datei liest; `Process.run`, `Clock.milliseconds`, `createDirectory`; das
  Top-Level-`?` druckt `error: ...` und endet mit 1 (`ExitWithCode`); `using` über `shared type` ist jetzt ein
  sauberer Befund. Sechs neue native Gate-Programme. `torb build ../compiler` kommt bis `main.trb:333` (Spread im
  Listen-Literal - liegt beim Collection-Kern).
  - **Entschieden (ich):** Collections sind `Equals` (und `Hash`), wenn ihre Items es sind - Werte vergleichen
    strukturell, Tupel tun es schon (`extend<Item: Equals> List<Item> with Equals` und Geschwister). Ein
    `project.trb` ist kein Programmcode: das Back-End lowert es nicht (es wird statisch gelesen).
  - **Läuft (neuer Opus-Agent "Schwanz 2"):** Natives, die eine Collection liefern/nehmen (`String.split`,
    `File.list`), Feld-Defaults von Case-Konstruktoren, `Equals` der Collections, Closure über `var self`,
    `parser.trb:197`, quotierte Ausdrücke 5.11 (für `torb test` nativ), die drei internen Fehler außerhalb des
    Compilers. Notiert für 5.14: `Char.toUpperCase` ist in der Runtime nur ASCII.

- (Schreibregel, ASCII-Namen, ungelesene Pattern-Bindung, 2026-09-21)
  - **Erledigt:** Beide Lexer lesen ein Wort als ganzen Lauf und melden **einen** Fehler dafür ("A name is written in
    ASCII letters, digits and `_`"); das Token bleibt ein `Identifier`, damit über die Zeile nichts zweimal gemeldet
    wird. `startsUpperCase` ist in beiden Parsern `A`-`Z`. Die Schreibregel ist ein eigener syntaktischer Pass
    (`compiler/src/semantics/checker/spelling.trb`), eine Meldung pro Deklaration am Namen. Die ungenutzte Bindung ist
    ein Fehler in den refutablen Positionen (`match`-Arm, `if const`/`if var`, `while const`), `_name` ist ausgenommen.
    `torb canon --rule unused-bindings` ist da und im Gate von CONTRIBUTING.
    - **Das Repository hatte keine einzige Verletzung** - weder Schreibweise noch nicht-ASCII noch ungelesene Bindung
      (`check ..`: "no problems", der Sweep ändert 0 von 294 Dateien). Zu korrigieren waren nur die
      *In-Memory-Quellen* der Checker-Tests (Strings, für das Canon-Werkzeug unerreichbar): 14 Stellen in
      `exhaustive`, `statements`, `lower-match` und `check`.
    - Zwei Punkte, die die Regel so nicht abdeckte, selbst entschieden: eine Pattern-Bindung braucht keine
      Schreibprüfung (der Parser liest einen großen Namen dort als Case, also sind `for`-Bindung,
      Closure-Parameter und Arm-Bindung gar nicht groß schreibbar), und `const Limit = 10` - wo `Limit` als
      Case-Pattern geparst wird und deshalb bisher **gar nichts** deklarierte und **gar nichts** meldete - gilt jetzt
      als die Konstante, die gemeint war.
    - Die VS-Code-Grammatik und `torb highlight` akzeptierten schon nur ASCII (`[A-Za-z_][A-Za-z0-9_]*`, in
      `extension.js` `[A-Za-z_]\w*` ohne `u`-Flag); geändert wurde dort nur `starts_upper_case` auf `A`-`Z`.
    - `docs/language/syntax/naming-conventions.md` heißt jetzt `naming.md`: die Seite sagte "today nothing enforces
      either rule", und zwei Drittel davon sind jetzt Diagnosen.

- (Encode/Decode neu gedacht, 2026-09-21) **Anlass (Nutzer):** kein XML-Sonderfall im Vokabular ("die anderen
  zigtausend Formate kriegen auch keinen"), kein zweiter Eingang (`decode`/`read`), und das "magische, alle Felder
  fressende" Ableiten hakt noch. Gewünscht: implizit, automatisch, bei Bedarf anpassbar, eigene Formate simpel
  einzubringen, Anpassung im DSL-Stil statt Meta-Programmierung.
  - **Vorschlag (ich, Details im Chat, wartet auf das Okay des Nutzers):**
    (1) EIN Prinzip statt einer Liste magischer Traits: *ein Wert ist sein Konstruktoraufruf.* Der Compiler kennt den
    Konstruktor und bietet ihn in drei Formen an - geschrieben (`Encode`), gelesen (`Decode`), beschrieben
    (NEU, Arbeitsname `Describe`: die Struktur OHNE Wert, mit Feld-Docblocks und Defaults - für SQL-DDL, Protobuf-/
    Avro-/JSON-Schema, OpenAPI, CLI-Hilfe). Abgeleitet wird genau dann, wenn der Konstruktor von außen benutzbar
    ist, und genau über seine Parameter - private Felder mit Default sind nicht dabei (kein Leck, "überspringen"
    ohne Annotation). (2) Was an EINEM Typ in EINEM Format besonders ist (XML-Attribut, Protobuf-Feldnummer,
    SQL-Spaltentyp, CSV-Spaltenfolge, Bitbreite), ist weder Annotation noch zweiter Trait noch Vokabel in
    `std/encoding`, sondern ein Mapping-WERT in der DSL des Formatpakets, mit typgeprüften Feldbezügen über quotierte
    Ausdrücke (`attribute { _.currency }`); das Format findet ihn beim Laufen über den qualifizierten Typnamen, den
    `record(typeName, ...)` ohnehin trägt - deshalb wirkt er auch verschachtelt, ohne Typtest und ohne
    Spezialisierung. Derselbe Weg für wohlbekannte Typen (`Instant` → CBOR-Tag/BSON-Datum). (3) `XmlEncode`/
    `XmlDecode` entfallen; Dokument-XML ist der Baum `XmlNode`. (4) `Encode`/`Decode` bleiben implizit (mein
    Vorschlag "ausdrücklich per `with`" ist damit zurückgezogen); die Fehlermeldung an der Aufrufstelle nennt die
    Feldkette. Umsetzung: Design-Dokument `docs/ENCODING.md`, zusammen mit "Encoding auf statischen Dispatch".
  - **Entschieden (Nutzer, 2026-09-21):** Richtung angenommen ("finde das Konzept erst mal besser"), sauber mit
    `Describe` durchplanen und mit ein paar Formaten durchtesten, ob es einfach und gut zu nutzen ist.
    **Wird gelöst (Opus-Agent, nur Design + Labor, kein Eingriff in std/Compiler):** `docs/ENCODING.md` und
    `examples/encoding-lab/` - die neuen Traits lokal, für Beispieltypen von Hand das, was der Compiler ableiten
    WÜRDE, und dagegen sieben Formate: JSON, CSV, XML mit Mapping-DSL (verschachtelt!), ein Protobuf-artiges
    Binärformat mit Feldnummern, SQL-DDL + Zeilenbindung, CLI-Argumente mit `--help` aus den Docblocks, und ein
    "in zehn Minuten erfundenes" Format als Test für "eigenes Format simpel einbringen". Dazu ein ehrliches
    "Wie fühlt es sich an" (Zeilen für Nutzer und Formatautor, Vergleich mit serde/kotlinx) und die Liste, was
    Sprache/Compiler dafür liefern müssen (u. a. statischer Dispatch: was wird aus `Encode` als Typ?).
  - **Erledigt (Entwurf + Labor, Gates grün):** `docs/ENCODING.md` und `examples/encoding-lab` (Vokabular lokal, vier
    Beispieltypen mit dem von Hand geschriebenen "was der Compiler ableiten würde", sieben Formate: JSON, CSV, XML mit
    Mapping-DSL, protobuf-artiges Binärformat, SQL-DDL + Zeilenbindung, Kommandozeile, logfmt; 34 Tests).
    - **Drei Dinge, die das Labor am Entwurf geändert hat.** (1) NEUE Regel: *ein Record, der kein Feld anmeldet, ist
      ein Wrapper.* `with Encode by value` erzeugt `record(typeName)` + einen Wert + `finish()`. Ohne sie verliert ein
      wohlbekannter Typ seinen Namen und Punkt 2 des Vorschlags (Mapping über den Typnamen) funktioniert für
      `Instant`/`Email` überhaupt nicht - JSON schreibt trotzdem nur den Wert, SQL liest den Namen und wählt
      `VARCHAR(320)`. (2) Das Vokabular wird FLACH: ein Trait je Richtung statt vier, `field` + `finish` statt
      Unter-Encodern (die bräuchten assoziierte Typen, die es nicht gibt). (3) Die Leseseite jedes puffernden Formats
      ist ein Parser plus EIN geteilter `ValueDecoder` - deshalb kostet logfmt nur 116 Zeilen.
    - **Offene Fragen an dich** stehen in Abschnitt 15 von `docs/ENCODING.md` (Name `Describe` neben der Funktion
      `describe`; gehört `EncodedValue` ins Prelude; darf ein Format ein unbekanntes Feld ablehnen; wie viel
      Tabellen-Hilfe gehört in `std/encoding`).
    - **Nächster Schritt:** die acht Umsetzungsscheiben aus Abschnitt 14, jede für sich grün.

- (Dokumentation am Code, 2026-09-21) **Erledigt: Standard + Gate, gemergt, Gates grün** (1422 Tests). `torb docs
  source <pfad>` prüft Docblocks (fehlend, fremde Überschrift, Link, Beispiel wird geparst + typgeprüft, Wörter der
  Vergangenheit); `--statistics` zeigt die Abdeckung. Modul-Kommentar = der Docblock, mit dem die Datei BEGINNT (vor dem
  ersten `use`; keine Parser-Änderung nötig). Vorlage: `std/core/src/option.trb` + `result.trb`. **Stand:** std 40 %
  (360/897), Compiler 84 % (854/1009) aber 395 Vergangenheits-/Plan-Wörter ("milestone 5.13", "gap 14" - werden zu
  `# Open` im Präsens, das ist echtes Umschreiben), Examples 6 % (10/156).
  - **Läuft: Welle 1** (Sonnet-Schreiber, je eigener Worktree, Gate als Abnahme): `examples/` und die std-Pakete, die
    gerade keine Code-Runde anfasst (stream, io, fs, iteration, http, task, process, environment, console, number,
    math, time, test, expression, sandbox, project). `core`, `collections`, `prelude`, `text` nach den laufenden
    Code-Runden; `encoding`, `json` nach dem Encoding-Design; Compiler = Welle 2.

- (Collection-Kern, 2026-09-22) **Erledigt, gemergt (`e9db188`), volle Gates grün** (1428 Tests, 94 Runtime-Tests,
  volle `cargo test`): Compiler **99 % gelowert (12680 von 12745), 0 interne Fehler**; `torb build ../compiler` meldet
  noch VIER Probleme (Closure über `var self` in `lexer.trb:543`, dreimal das generierte `compare` eines Tupels) -
  das ist die ganze Strecke bis Meilenstein 6.1 und liegt bei "Schwanz 2". Index-Pfade = Herausnehmen/Zurücklegen
  über `at`/`set`; Map-/Set-Cursor (eine Runtime-Funktion, Rest TorbScript); `sort` (stabiler Merge-Sort) und `slice`
  sind Defaults von `List`; Varargs sind `List<Item>`, ein Spread ist ein `add` pro Item. `print` bleibt bewusst die
  schnelle Zwei-Instruktionen-Form. Drei neue Gate-Programme.
  - Der End-to-End-Test liest die Position eines `std/`-Frames jetzt als `_:_`: ein Docblock über einer Panic-Stelle
    verschiebt die Zeile (so brach der Test nach dem Merge des Doku-Standards).
- (Dokumentation am Code, Welle 1, 2026-09-22) **Erledigt, gemergt:** `examples/` 156/156, `std/stream`+`io`+`fs`
  95/95, `std/iteration` 118/118, zwölf kleine std-Pakete 268/268 - alle "no problems" im Gate. Funde der Schreiber:
  ein echter Fehler (`std/stream/src/bytes.trb:180`: `Utf8Error(0)` mit fest verdrahtetem Offset), zwei Doc-Beispiele,
  die nie kompiliert hätten (`Process`, `Sandbox.load`), zwei Widersprüche Kommentar/Doku (`Decimal`, `std/project`),
  mehrere bisher undokumentierte Pitfalls (`minBy`/`maxBy` bei Gleichstand, `Buffered.flush`, `Process.run`).
  - **Offen:** drei Schwächen des Gates (Beispiele in Nicht-`lib.trb`-Dateien finden die freien Funktionen derselben
    Datei nicht; Plural `milestones` wird nicht erkannt; `no longer` schlägt auch harmlos an) und zu wenige Beispiele
    (16 auf 537 Deklarationen) - nach der Gate-Reparatur eine gezielte Beispiel-Runde für die Hauptkonstrukte.

- (Encoding-Design, 2026-09-22) **Entschieden (Nutzer: "mach das erst mal alles so"), in `docs/ENCODING.md`
  Abschnitt 15 eingetragen:** (1) der Trait heißt `Describe`, die freie Funktion `describe(value)` wird zu
  `rendered(value)`; (2) `EncodedValue` kommt ins Prelude; (3) ein Format darf ein unbekanntes Feld ablehnen - als
  Option `strict: true`, Standard bleibt tolerant, einheitlich für die ganze std; (4) "Record als flache Pfade" kommt
  als Komfort neben `Structure` nach `std/encoding`. Umsetzung in den Scheiben des Migrationsplans NACH dem Fixpunkt
  (Scheibe 1 braucht monomorphisierte generische Trait-Methoden im Back-End).
- (Gate-Reparatur, 2026-09-22) **Erledigt, gemergt:** Beispiele sehen die `public`-Namen ihrer eigenen Datei (Ursache:
  ein nicht-öffentliches Top-Level-`const` ließ das Gate die Datei für ein Skript halten); Plurale
  (`milestones`, `gaps 12`); `no longer` nur vor einem Verb in der dritten Person; `Utf8Error` trägt bei
  `decodedText()` den Offset ab Stream-Beginn; erster Test unter `std/` (`std/stream/tests/bytes.test.trb`).
- (**Rot auf master, in Arbeit**, 2026-09-22) Zwei Tests (Instanzzahl-Stolperdraht, 121 → 192 Deklarationen) schlagen
  fehl, seit `examples/encoding-lab` im Repository liegt. Ursache ist ein Entwurfsfehler im Back-End, kein Fehler des
  Labors: "überschreibt irgendeine Implementierung diesen Default?" wird über ALLE gelesenen Dateien des Workspace
  gefragt statt über das Programm. **Entschieden (ich):** die geschlossene Welt ist das Programm (die vom
  Wurzelmodul aus erreichbaren Module), nie ein Paket, von dem es nicht abhängt - ein Nachbarpaket darf dein Binary
  nicht verändern. Liegt als erster Punkt bei "Schwanz 2".

- (Sichtbarkeit, `loop`, `??`, Kleinigkeiten, 2026-09-22) **Erledigt, im Zweig grün** (1446 Tests, `check ..` ohne
  Probleme, `docs check`/`docs index --check` grün). Die vier Regeln stehen in CONCEPT (Entscheidungslog),
  `docs/TYPECHECKER.md` (Lookup-Reihenfolge, Operatoren) und in den Doku-Seiten; neu ist
  `docs/language/execution/loops.md`, weil es für `while`/`loop` überhaupt keine Referenzseite gab.
  - **Kleinigkeit 1 erledigt:** `const None = x` / `const Some(y) = x` melden jetzt den gewöhnlichen
    Refutable-Pattern-Fehler. Zwei Ursachen: der Schreibregel-Pass beanspruchte den nackten Großbuchstaben-Namen als
    falsch geschriebene Konstante und verschluckte damit den Befund (`reportHere` lässt nur einen pro Span), und eine
    Top-Level-Bindung, die keinen Namen deklariert, hat ihr Pattern nie geprüft. Nebenbei gefunden und geschlossen:
    ein nackter Case-Name gegen einen Typ **ohne** Cases (`const None = 1`, `match 1 { None => ... }`) war ganz
    stumm, weil die Meldung dafür in einem Zweig lebt, der Cases voraussetzt.
  - **Kleinigkeit 3 erledigt:** `Map.iterator` liefert `Iterator<(key: Key, value: Value)>`. Die Labels überleben die
    Inferenz durch `map`, `all`, `for` und Destructuring - geprüft; `for (name, age) in ages` und `Map.from(pairs)`
    bleiben unverändert, weil ein Label nicht zum Typ gehört. In std und Beispielen sind die drei Stellen mit
    `entry.0`/`entry.1` auf `entry.key`/`entry.value` umgestellt.
  - **Aufgefallen, nicht angefasst:** `indexed()` liefert weiter `Iterable<(Int, Item)>` ohne Labels
    (`(index:, item:)` würde sich gleich gut lesen) - gehört in eine eigene kleine Runde, wenn gewünscht.

- (**Meilenstein 6.1 erreicht**, 2026-09-22) Gemergt, alle Gates grün (1452 Tests, 95 Runtime-Tests, volle
  `cargo test`, 30 native Gate-Programme byte-gleich mit Stage 0). **Von mir nachgeprüft:** `torb build ../compiler`
  schreibt in 4,5 Minuten `compiler/build/release/main.exe` (11 MB, 66 MB C); das Binary prüft das ganze Repository in
  **6,9 Sekunden** ("279 files, no problems") und gibt bei `ir --statistics ../compiler` byte-gleich dasselbe aus wie
  Stage 1. Compiler und Repository 99 % gelowert, 0 interne Fehler; offen in `compiler/` sind nur die 58 quotierten
  Ausdrücke in den Tests.
  - Closed-World-Fehler behoben: die Welt sind die PAKETE des Programms (Wurzeln, Abhängigkeiten, Prelude);
    Stolperdraht 115/163/38/3; zwei Tests nageln es fest. Die zwei roten Tests sind grün.
  - Was erst der selbstgebaute Compiler fand: eine Kopie eines geboxten Werts sah die Änderung am Original (der Parser
    kopiert sich beim Backtracking); `!=` wurde für jeden Typ mit aufgerufener Gleichheit als `==` gelowert.
  - **Offen bis 6.2 (Fixpunkt):** das Binary HÄNGT beim Bauen des Compilers (10 Minuten, keine Zeile C; Front-End,
    Lowering und Ownership stimmen byte-gleich - es bleiben `verifyOwnedProgram` und der Emitter mit ihren
    `while isChanged`-Schleifen). Danach: quotierte Ausdrücke 5.11 (`torb test` nativ), `Float64.compare`-Wrapper,
    Collection aus einem Trait-Wert lesen (7.3).
- (Checker-Runde, 2026-09-22) **Erledigt, gemergt (`e9b0d06`):** Extension-Sichtbarkeit (`use Int64.seconds from
  "std/time"`, `as`; das eigene Paket sieht seine Extensions ohne Import; Prelude re-exportiert genau eine Zeile),
  `loop` (17 Stellen umgestellt), `??` über `OrElse`, `const None = x` ist ein Fehler, `Map.iterator` liefert
  `(key:, value:)`. Notiert: `indexed()` könnte `(index:, item:)` liefern; `reportHere` behält nur EINE Meldung pro
  Span und verschluckt die zweite.
- (Läuft, 2026-09-22) **Fixpunkt-Agent (Opus):** den Hänger von Stage 2 finden (klein reproduzieren: jedes
  Gate-Programm mit `main.exe` bauen und das C gegen Stage 1 diffen), Fixpunkt-Test ins Repository, danach
  `Float64.compare`-Wrapper, quotierte Ausdrücke 5.11, Zeit-/Speichermessung Stage 1 gegen Stage 2.
  **Doku-Welle 2 (sieben Sonnet-Schreiber):** `std/core`+`prelude`+`text`, `std/collections` (beide mit Schwerpunkt
  Beispiele), Compiler `syntax`+`project`+`cli`, `semantics` in zwei Hälften, `documentation`, Datei-Kommentare und
  Vergangenheitswörter der Tests. `ir/`, `backend/` und `std/encoding`+`json` folgen nach Fixpunkt bzw.
  Encoding-Umbau.

- (Dokumentation am Code, Welle 2, 2026-09-22) **Erledigt, gemergt (`7ce4445`), volle Gates grün** (1452 Tests, volle
  `cargo test`): `std/core`+`prelude`+`text` 151/151 (19 Beispiele), `std/collections` 201/201 (17 Beispiele), Compiler
  `syntax`+`project`+`cli` 105/105, beide Checker-Hälften (268 Plan-/Vergangenheitswörter → 0), `documentation`
  140/140, alle 57 Testdateien mit Datei-Kommentar. **Das Gate meldet fürs ganze Repository noch 343 Probleme**
  (Start: 1510) - das sind `compiler/src/ir`+`backend` (nach dem Fixpunkt), `std/encoding`+`json` (nach dem
  Encoding-Umbau) und `examples/encoding-lab`.
  - Neu am Code sichtbare Pitfalls: `Into` direkt implementieren kollidiert mit dem Blanket über `From`; nichts
    erzwingt, dass `compare` und `equals` übereinstimmen; `MutableSlice.replace` lässt die Länge offen;
    `counters[0].increment()` ändert an Ort und Stelle, `var first = counters[0]` kopiert; `Sandbox.load` erkennt nur
    Pfad-LITERALE.
  - Notiert: Gate hält `...` in Backticks für ein Satzende (Fehlalarm "wiederholt den Namen"); Back-End-Meldungen an
    Nutzer tragen "(milestone 5.9b)" - Frage an den Nutzer gestellt, ob das raus soll; `mutation.trb:78` vermutlich
    toter Schutz.
- (UFCS und Überladung, 2026-09-22) **Antwort im Chat, Empfehlung: beides nicht.** UFCS braucht Überladung, weil freie
  Funktionen EINEN Namensraum pro Modul haben (`first`, `length` gäbe es je einmal); ein `extend` ohne Trait IST die
  über den ersten Parametertyp benannte Funktion. Ad-hoc-Überladung kollidiert mit der tragenden Eigenschaft der
  Sprache: die Signatur des Aufgerufenen bestimmt, wie das Argument gelesen wird (`.Case`, `None`, Listen-Literal als
  `Array`, `{ _ }`, `lazy`, `Expression<T>`, `var`-Platz, Receiver-Closure). Vorhanden und prinzipientreu sind zwei
  Formen: Überladung nach dem EMPFÄNGER (Methoden/`extend`) und über TRAIT-PARAMETER (`From<Source>`,
  `Multiply<Other, Output>`). Wartet auf die Rückmeldung des Nutzers, wo ihm Überladung konkret fehlt.
  - **Entschieden (Nutzer, 2026-09-22): "wir machen das wie du sagst"** - kein UFCS, keine Überladung nach
    Parametertyp; die zwei vorhandenen Formen (Empfänger, Trait-Parameter) sind die Antwort. Im Decision Log von
    CONCEPT eingetragen; eine Erklärseite ("Where are my overloads?") kommt mit der nächsten Doku-Pflege. Ebenso
    entschieden: die Plan-Nummern ("(milestone 5.9b)") kommen aus den Back-End-Meldungen an Nutzer heraus - zusammen
    mit der Doku-Welle für `ir/`+`backend/` nach dem Fixpunkt (die Meldungen sind durch Tests festgenagelt).


- (**Meilenstein 6.2, der Fixpunkt**, 2026-09-20) **Er hält: das C von Stufe 1 und das von Stufe 2 sind byte-gleich,
  und Stufe 3 schreibt dasselbe noch einmal.** Der "Hänger" war kein Fehler im Back-End, sondern eine quadratische
  Bibliotheksfunktion, die nur ein kompiliertes Programm bezahlt.
  - **Ursache:** `Iterable.joined` faltete `result + separator + item.show()`. Ein `String` ist ein Wert, also kopiert
    jeder Schritt alles, was schon drin steht - `n` Stücke kopieren `O(n²)` Bytes. Und **Stage 0 hat davon nie eine
    Zeile ausgeführt**: `joined`, `String.from` und die ganze `Iterable`-Fläche sind Natives des Interpreters, und
    Rusts `parts.join(separator)` läuft einmal über die Bytes. Das `program.c` des Compilers hat 786457 Zeilen und
    66 MB, und `emittedText` ist EIN `joined` darüber - hochgerechnet über drei Stunden Kopieren, ohne ein Byte
    Ausgabe. Gemessen an einer Sonde (60000 Stücke, 1,4 MB): **20,6 s vorher, 0,068 s nachher, 0,065 s auf Stage 0.**
  - `concatenated(pieces, separator)` (neu, `std/iteration/src/concatenate.trb`) mischt **Nachbarn paarweise**: jedes
    Byte wird einmal pro Ebene des Mischbaums kopiert, und es gibt `log n` Ebenen. Gleiche Reihenfolge, gleiche Anzahl
    Trenner. Dieselbe Form war auch in `String.from(Iterable<Char>)` (ebenfalls von einer Stage-0-Native verdeckt) und
    im `joining`-Collector. Nebenbei einen echten Fehler mitgenommen: ein **führendes leeres Stück** hat seinen Trenner
    verschluckt (`["", "b"].joined(separator: ",")` war `b` statt `,b`), weil `result.isEmpty()` "noch nichts" nicht
    von "der leere Text" unterscheiden kann.
  - **Zweite Abweichung, die erst der Fixpunkt fand:** `Process.run` lief über eine Shell, und deshalb fand das Binary
    keinen C-Compiler ("no C compiler found", während der Interpreter gcc im gleichen PATH fand). `_popen` startet
    `cmd /c`, und `cmd` liest die Anführungszeichen nach einer Regel neu, die davon abhängt, wo das erste steht:
    `"gcc" "--version"` kommt als EIN Kommando namens `gcc" "--version` an. Mit einer C-Sonde durchprobiert - es gibt
    keine Zitierweise, die das UND ein verschachteltes `cmd /c "echo torb"` übersteht, also ist die Shell weg:
    `CreateProcess` plus eine Pipe. Damit stimmen zwei Versprechen wieder, die der Interpreter schon hielt ("es gibt
    keine Shell", und ein Programm, das gar nicht startet, ist ein Fehler und kein Exit-Code). POSIX behält `popen`
    mit einfachen Anführungszeichen; `fork`+`execvp` wäre dort die gleiche Reparatur und gehört zu 7.3.
  - **Der Rest stimmte schon:** vor der Reparatur wurden alle 41 Gate-Programme von Stufe 1 und Stufe 2 emittiert und
    byte-weise verglichen - **41 von 41 gleich**. Keine Iterationsreihenfolge, kein nicht initialisierter Slot, kein
    adressabhängiger Hash, kein Float-Format.
  - **Der Fixpunkt-Test** ist `bootstrap/crates/torb-cli/tests/fixpoint.rs`, `#[ignore]`d weil er Minuten braucht:
    `cargo test --release --test fixpoint -- --ignored --nocapture`. Er meldet bei einem Unterschied das **erste
    abweichende Byte mit dem Text drumherum** in beiden Dateien.
  - Danach mitgenommen: **`Float64.compare` hat jetzt einen Wrapper** (`NativeResult.Ordering` - die Runtime antwortet
    ein Vorzeichen, der Wrapper die drei Fälle; dieselbe Form für `Instant.compare`/`Duration.compare`), also kann ein
    Tupel mit Float-Feld emittiert werden und das Float-Feld ist in `tuple-compare.trb` zurück. Ein `nan` kann dort
    **nicht** stehen: Stage 0 weigert sich, einen zu vergleichen ("the Float NaN and the Float 1.5 cannot be
    compared") - eine weitere Zeile auf der 5.14-Liste.
  - **Offen, in der Reihenfolge:** quotierte Ausdrücke 5.11 (58 Befunde, alle in `compiler/tests/`; `torb test` nativ),
    eine Collection aus einem Trait-Wert lesen (7.3), und 6.3s Messungen - `joined` ist der Grund, dort nach mehr vom
    gleichen zu suchen (ein Build pro Lesen eines Modul-`const`, die Instanzzahl).

- (**Meilenstein 6.2 erreicht: der Fixpunkt hält**, 2026-09-23) Gemergt, alle Gates grün (1453 Tests, 96 Runtime-Tests,
  volle `cargo test`, 42 native Gate-Programme). **Von mir nachgeprüft** (`cargo test --release --test fixpoint --
  --ignored --nocapture`): Stage 1 baut den Compiler in 245 s, Stage 2 in 114 s, beide erzeugen dieselben 65,7 MB C
  Byte für Byte, Stage 3 emittiert es in 20 s ein drittes Mal identisch.
  - Ursache des Hängers: `Iterable.joined` war quadratisch (ein `String` ist ein Wert, jeder Schritt kopierte den
    ganzen Akkumulator); Stage 0 versteckte das hinter einem Rust-Native. Jetzt `concatenated` als Merge-Baum.
    Neue Regel in CONTRIBUTING: eine Kostenfrage klärt nur ein KOMPILIERTER Probelauf.
  - `Process.run` läuft unter Windows ohne Shell (`CreateProcess`) - vorher fand das Binary keinen C-Compiler.
  - Stage 2 gegen Stage 1: `check ..` 7,2 s statt 47 s (216 MB statt 726 MB); den Build dominiert jetzt gcc
    (96 von 114 s für EINE 65-MB-Datei) - das ist die 6.3-Frage "Übersetzungseinheit aufteilen".
  - **Nächstes ("Schwanz 3"):** quotierte Ausdrücke 5.11 (`main.exe test`), 6.3 (Übersetzungseinheit aufteilen,
    Konstanten als unsterbliche gezählte Statics, Instanzzahl), Plan-Nummern aus den Back-End-Meldungen, Doku-Welle
    `ir/`+`backend/`. Danach wie geplant: `std/linear` + `std/geometry`, der Encoding-Umbau, 5.14 Konformität, 7.x VM.
- (Läuft, 2026-09-23) Drei Opus-Agents und vier Sonnet-Schreiber parallel: **Schwanz 3** (quotierte Ausdrücke 5.11 →
  `main.exe test`; 6.3: gcc/Übersetzungseinheit, Konstanten, Instanzzahl; zuletzt Plan-Nummern aus den Meldungen),
  **Konformität 5.14** (Panic-Format und Exit-Code in Stage 0, alle notierten Abweichungen mit je einem
  Gate-Programm, `language.trb`, der Konformitäts-Runner als benannter Vertrag), **`std/linear` + `std/geometry`**
  (mit `docs/LINEAR.md`, Trait `Real`, `Fixed` als deterministischer Skalar - ausdrücklich auch als Härtetest: was die
  Sprache nicht kann, wird mit Reproduktion gemeldet), **Doku-Welle 3** (`ir/`, `ir/lower`, `backend/c`,
  `examples/encoding-lab`).

- (Rolle von `Array`, 2026-09-23) **Anlass (Nutzer):** `native fn of(...items: Item): Array<Item, Size>` ist
  unintuitiv - die Signatur sagt "variadisch, also Liste", und Compiler-Magie löst daraus `Size` und erlaubt einen
  Spread nur aus einem anderen `Array`. **Entschieden (Nutzer, auf meinen Vorschlag):** `Array.of` entfällt und mit ihm
  der Checker-Sonderfall `checkArrayOf`. `Array<Item, const Size: Int>` ist das INLINE-SPEICHERPRIMITIV (feste Größe,
  kein Heap, neben dem geplanten `Buffer<Item>` für den Heap), keine weitere Collection. Es entsteht nur auf Wegen,
  deren Signatur ehrlich ist: das Listen-Literal gegen einen erwarteten Array-Typ (Anzahl geprüft; die Anzahl steht nur
  im Literal syntaktisch fest - so machen es Rust, Swift 6.2, Zig, Go), `Array.filled(value)`, NEU
  `Array.generated { index => ... }` (beide: `Size` aus dem erwarteten Typ, gewöhnliche Inferenz) und
  `Array.from(items): Array<Item, Size>?`. Die Größe wird vorerst ausgeschrieben (der Fehler nennt die richtige
  Zahl); `_` als Typargument ("leite ab", `Array<Int, _>`, allgemein auch `Map<String, _>`) ist als Kandidat notiert.
  **Wird gelöst:** kleine Runde zusammen mit den notierten Kleinigkeiten (`indexed()` mit Labels, `reportHere`
  verschluckt die zweite Meldung, Gate-Fehlalarm bei `...` in Backticks, toter Schutz in `mutation.trb`, Erklärseite
  "Where are my overloads?").
  - **Ergänzt (Nutzerfrage "gehört Array eher in den core?", 2026-09-23) - Entschieden (ich): ja.** `std/core` hält,
    was die Sprache selbst kennt (`Option` hinter `T?`, `Result` hinter `?`, `Range` hinter `a..b`) - und `Array` ist
    das Inline-Speicherprimitiv, an das sich ein Listen-Literal anpasst (der Checker kennt es beim Namen).
    `std/collections` hält Datenstrukturen AUF den Speicherprimitiven; der geplante Heap-Kern `Buffer<Item>` kommt
    aus demselben Grund neben `Array` nach `std/core`. Das Prelude exportiert `Array` weiter, für Programme ändert
    sich nichts. Der Agent der kleinen Runde zieht es mit um.

- (`From`/`Into` und Kohärenz, 2026-09-23) **Anlass (Nutzer):** der Pitfall an `Into` ("implementiere `From`, nie
  `Into`") ist doof; Wunsch: eines implementieren, das andere geschenkt bekommen, oder beide von Hand.
  **Antwort:** beides scheitert - zwei gegenseitige Blankets sind zirkulär (jedes Paar wäre "konvertierbar", zur
  Laufzeit Endlosrekursion), und beide von Hand heißt zwei Wahrheiten für EINE Konvertierung. **Aber dahinter steckte
  ein echtes Loch:** den EIGENEN Typ in einen FREMDEN konvertieren ging gar nicht (`extend Float64 with
  From<Celsius>`: weder Typ noch Trait gehört mir; `extend Celsius with Into<Float64>`: kollidiert mit dem Blanket) -
  der Stand von Rust vor RFC 2451.
  - **Entschieden (Nutzer: "mach a und b"):** (a) die Besitzregel zählt auch einen als TRAIT-ARGUMENT genannten
    eigenen Typ (`extend X with Trait<Meins>`); eindeutig bleibt es, weil nur der Besitzer von `Celsius` die Regel
    darüber erfüllt. (b) Wer einen Trait mit Blanket von Hand implementiert, bekommt eine gezielte Meldung, gebildet
    aus der `where`-Klausel des Blankets ("`Into` comes from `From` ...: write `extend Float64 with From<Celsius>`");
    der Pitfall im Docblock entfällt damit. **Wird gelöst:** in der laufenden kleinen Checker-Runde.
  - **Ergänzt (Nutzerfrage "was ist mit `TryInto`? `Parse` streichen?", 2026-09-23) - Entschieden (Nutzer: "machen
    wir so"):** `TryInto<Target, Failure>` kommt mit Blanket über `TryFrom` dazu (Symmetrie zu `Into`; man
    implementiert `TryFrom`, nie `TryInto`; kein Blanket "jedes `From` ist ein `TryFrom`", das überlappte jedes
    handgeschriebene). `Parse` bleibt ein eigener Trait: `From`/`TryFrom`/`Into`/`TryInto` konvertieren zwischen
    WERTtypen, `Show`/`Parse` stehen zwischen einem Wert und seinem TEXT und sind Partner; Text wird IMMER über
    `Parse` zum Wert, nie über `TryFrom<String>` (später als Lint). Der Vorschlag "`Parse: TryFrom` mit
    Default-`parse`" ist nicht ausdrückbar: eine Funktion ohne `self` MIT Rumpf in einem Trait gehört dem Trait selbst
    (`List.of`), sie ist kein Default der Implementierer; ein Blanket-Alias gäbe drei Schreibweisen für einen Aufruf.
  - **Revidiert (Nutzer, 2026-09-23): `Parse` wird gestrichen.** Sein Einwand trägt: `Show` ist unser `toString()`
    (Anzeige und Debuggen, ein Mix aus Rusts `Display` und `Debug`) und nicht der Partner eines Parsers - das
    abgeleitete `Show` eines Records liest auch kein `Parse` zurück; meine Paar-Begründung war schwach, und meine
    Trennregel hätte einen Lint gebraucht, um zu halten. `Email.tryFrom "a@b.com"` ist in Ordnung. **Entschieden:**
    Text wird wie jede andere fehlbare Konvertierung über `TryFrom<String, Failure>` zum Wert (und über `tryInto()`
    in einer Kette); damit gibt es genau EINEN Weg und keine Regel, die jemand überwachen müsste. Betrifft: den Trait
    in `std/core`, `Numeric` (`Parse<NumberParseError>`), das abgeleitete `Parse` der Literaltypen (wird abgeleitetes
    `TryFrom<String, LiteralParseError>`), Checker (`derive.trb`, `implementation.trb`), Lowering (`generic.trb`,
    `witness.trb`), Stage 0 (`Int.parse`, `Float.parse` in `natives.rs` - `tryFrom` muss dort nach dem Laufzeittyp des
    Arguments unterscheiden), Tour, Encoding-Labor, Doku. Funktionen, die bei einem FORMAT `parse` heißen
    (`Json.parse`), sind kein Trait und bleiben. **Eigene Runde, NACH Schwanz 3 und der Konformitätsrunde** (beide
    arbeiten in genau diesen Dateien). `TryInto` kommt wie besprochen schon mit der kleinen Runde.

- (Offene Ranges und Panics in std, 2026-09-23) **Anlass (Nutzer):** die `expect`s in `Range<Int>.iterator`/`length`
  gefallen ihm gar nicht. Zu Recht - und der Kommentar daneben ("would need dependent types") ist falsch.
  **Entschieden (Nutzer: "passt alles so"):** der TYP sagt, welche Enden es gibt, gewählt von der Syntax:
  `a..b`/`a..=b` → `Range<Value>` (`start`, `end`, `inclusive`; für `Int` `Iterable` + `Length`), `a..` →
  `RangeFrom<Value>` (für `Int` `Iterable`, endlos, kein `Length`), `..b`/`..=b` → `RangeTo<Value>` (weder noch).
  Kein Feld ist mehr optional, kein `expect`; `for i in ..10` und `(0..).length()` werden Compile-Fehler an der
  Schreibstelle. Was jede Form annimmt (Slices: `text[2..]`, `items[..3]`), nimmt den kleinen Trait `Bounds<Value>`
  (`lowest()`, `highest()`, `contains` als Default). `inclusive` bleibt ein `Bool`-Feld (Rust zieht es in den Typ und
  hat deshalb sechs Typen). Range-Patterns im `match` sind Syntax und unberührt.
  - **Dazu eine std-Designregel für CONTRIBUTING:** ein Panic in std ist nur für Aufruferfehler, die KEIN Typ
    ausdrücken kann (Index außerhalb); wo der Typ es sagen kann, sagt es der Typ. Die rund zehn Panic-Stellen in std
    werden in derselben Runde durchgesehen.
  - **Wird gelöst:** eine Aufräumrunde `std/core` zusammen mit der `Parse`-Streichung, NACH Schwanz 3 und der
    Konformitätsrunde (beide arbeiten im Lowering und in Stage 0).
- (Validierte Typen, 2026-09-23) **Antwort:** das gibt es schon - ein `private` Feld ohne Default macht den Konstruktor
  von außen unbenutzbar ("`Email` cannot be constructed here: `value` is private and has no default", durch ein
  Doku-Snippet festgenagelt); `copy` und das abgeleitete `Decode` folgen derselben Regel. Preis: das Feld ist von
  außen auch nicht lesbar (eine Zeile Accessor). Ein Modifier "öffentlich lesbar, privat konstruierbar" ist als
  Kandidat notiert, falls er mehr als einmal fehlt.


- (**Meilenstein 6.3, gemessen und ein Drittel erledigt**, 2026-09-20) **Der Build ist der C-Compiler, also ist die Frage
  nicht "wie schnell ist der Compiler", sondern "wie viel C schreibt er".** Alles gemessen auf einer Maschine
  (16 Threads, gcc 13.2, `-O2`); Zahlen unter Last sind als solche gekennzeichnet. Ausführlich in `docs/BACKEND.md`,
  Abschnitt "What 6.3 measured".
  - **Die Flags bleiben, und die Übersetzungseinheit wird NICHT aufgeteilt.** `-O2` 98,5 s; `-O1` 70,4 s (-29 %), macht
    den Compiler aber nur 3-8 % langsamer - das ist das Flag für ein Debug-Profil, wenn 5.13 Profile hat. `-O0` spart
    48 % und macht den Compiler 2,4x langsamer, also nein. `-pipe` ist unter Windows 11 % langsamer, die Warnungen
    kosten nichts (3 %, Rauschen). Ein echter 8-Wege-Split (jeder Rumpf im Ringverfahren, gemeinsamer Vorspann plus
    Prototypen für alle 35841 Rümpfe) übersetzt in **22,7 s statt 98,5 s (4,3x)** - aber **das Binary ist 2,28x
    langsamer** (`check ..` 16,5 s statt 7,2 s), weil sieben von acht Aufrufkanten dann eine Übersetzungseinheit
    kreuzen und nichts mehr inlinet. `-flto=8` wäre der Ausweg und geht mit dieser Toolchain nicht (Assembler: "too many
    sections", und dem gebündelten `ld` fehlt das LTO-Plugin). Also: weniger C emittieren, nicht aufteilen.
  - **Erledigt: ein Witness-Thunk heißt jetzt nach dem, was er ist.** Ein Thunk castet `void *self` auf das Payload-Layout
    des Ziels und ruft EINE Funktion des Programms - sonst steht nichts drin. Benannt war er nach *(Tabelle, Index)*,
    also waren `Accumulator.add`, `Collection.add` und `List.add` desselben Listentyps **drei Kopien einer Funktion**.
    Jetzt nach *(Funktion des Members, Payload-Layout)*: **16510 Thunks werden 8221**, das `program.c` des Compilers
    **65717562 -> 56994210 Bytes (-13,3 %)**, 787016 -> 725997 Zeilen, und gcc **111,8 -> 87,1 s (-22 %)** im gleichen
    Lastfenster. Namensgleichheit zweier verschiedener Thunks ist eine Kollision und kein Zusammenlegen: die Schlüssel
    eines Basisnamens werden sortiert und numeriert, damit der Name eine Funktion des Programms bleibt.
  - **Woher die 65 MB kommen** (eine Zählung, die byteweise aufgeht): 66,8 % Funktionsrümpfe, 13,8 % Prototypen, 13,7 %
    statische Daten. Nach Art: eigene monomorphe Funktionen des Compilers 24,0 %, Instanzen von Generics 18,1 %,
    Witness-Thunks 17,7 %. **Keine der 20 größten Definitionen ist eine Kopie** - die Verschwendung steckt im Schwanz:
    14794 von 35841 Rümpfen (41 %) sind byte-gleich mit einem anderen, wenn man den eigenen Namen wegnormiert.
  - **Der größte Hebel liegt noch da: die Namen sind zwei Drittel der Datei.** 419044 Vorkommen gemangelter Namen,
    43991922 Bytes, Mittel 105 Bytes; 53707 verschiedene Namen mit Mittel 139. Das `program.c` ist EINE
    Übersetzungseinheit, deren einzige äußere Symbole `main` und die der Runtime sind - kurze opake Symbole mit dem
    lesbaren Namen im Kommentar darüber wären **41058614 Bytes, 62,5 % der Datei**. Kostet inhaltlich nichts, nagelt
    aber jeden `torb ir`-Schnappschuss und jedes gepinnte C in den Tests um: eigene Runde.
  - **Ein Modul-`const`, das keine statischen Daten ist, wird bei JEDEM Lesen gebaut** - im emittierten C
    nachweisbar: `punctuationAt` des Lexers deklariert 105 Slots und baut alle 35 Tupel und die ganze Liste **in seinem
    eigenen Rumpf**, und es läuft einmal pro Interpunktionszeichen jeder Datei. Eine kompilierte Sonde (10 Paare,
    200000 Lesungen) misst **1,43 µs pro Lesen** gegen 0,83 µs für den Durchlauf allein. Der Lexer liest die Tabelle
    jetzt einmal pro Datei in ein eigenes Feld: "lexing, parsing and the module graph" über die 280 Dateien des Repos
    **2145/2130 ms vorher, 1858/1631 ms nachher** (14-23 % dieses Passes, und der Pass ist ein Drittel von `check`).
    Die allgemeine Form - ein **unsterblicher gezählter Static** pro `const`, einmal gebaut - ist noch nicht geschrieben,
    und es ist dasselbe Stück Emitter, das ein statischer `ExpressionNode`-Baum braucht.
  - **Der Fixpunkt hält auf der kleineren Datei:** Stufe 1 und Stufe 2 sind sich über 56994748 Bytes C einig,
    Stufe 3 schreibt sie noch einmal; Stufe 2 baut den Compiler in 112 s auf dieser Maschine.
  - **5.11 (quotierte Ausdrücke) ist gemessen, aber nicht implementiert** - die drei Kosten, die das Design entscheiden,
    stehen in BACKEND ("What 5.11 needs"): `describe` ist `.Planned` und kann keine Runtime-Funktion sein; **einen
    ganzen `Checker` zu zeigen kostet 1731 Instanzen** und scheitert heute an einem eigenen Befund
    (`a generated member without a receiver`, `std/core/src/convert.trb:113`), und genau solche Werte fangen die Tests
    des Compilers ein - also muss **eine** Funktion der Lowering entscheiden, wie eine eingefangene Variable gespeichert
    und gezeigt wird. Der Baum braucht den unsterblichen gezählten Static von oben. `test`/`group` können
    Runtime-Funktionen sein (die Runtime *kann* einen Closure aufrufen, jeder zeigt auf einen Thunk mit der gelöschten
    Signatur), brauchen aber einen Wiederaufsetzpunkt (`setjmp` in `panic.c`); ein aufgefangener Panic gibt nichts frei,
    also greift das Leck-Gate für ein Programm mit einem fehlschlagenden Test nicht. `main.exe test ../compiler/tests`
    ist EIN Binary für alle 55 Dateien (10810 Instanzen gegen 13629 des Compilers), kein Binary pro Datei.
  - **Nächstes:** 5.11 mit diesem Design, die unsterblichen gezählten Statics (Modul-`const` + Baum), die Namen im C,
    dann Plan-Nummern aus den Meldungen.

- (`std/path`, 2026-09-23) **Wunsch (Nutzer, mag Rusts `Path`), eingeplant NACH der Aufräumrunde `std/core`:**
  ein Werttyp `Path` im eigenen Paket `std/path` - EIN Typ (kein `Path`/`PathBuf`, wir haben Werte), intern Wurzel
  bzw. Laufwerk + Komponentenliste statt String, immer UTF-8 (ein Verzeichniseintrag mit ungültigem Namen ist ein
  Fehler, steht als Pitfall da), `joined`, `parent`, `name`, `extension`, lexikalisches `normalized()`; `Show` immer
  mit `/` (deterministisch, passt zu den Panic-Pfaden), die native Form samt `\?\` erst an der Grenze zum
  Betriebssystem. Gegen die Path-Traversal-Falle (`base.join("/etc/passwd")`): `joined` nimmt nur Relatives, dazu
  `resolved(inside: base): Result<Path, PathError>` - relevant für die Sandbox-Rechte.
  - **Signaturen:** `fn open(path: Into<Path>): Result<File, IoError>` - ein Trait ist bei uns ein Typ und
    `into(self): Target` ist objektsicher, also ohne Generics; ein String geht weiter direkt hinein
    (`extend Path with From<String>`, unfehlbar - ob ein Pfad gültig ist, sagt das Dateisystem beim Öffnen). Das ist
    unser `AsRef<Path>` und Designprinzip 1, keine implizite Konvertierung. Betrifft `std/fs`, `std/sandbox`,
    `Process.run(workingDirectory:)`; danach ersetzt es `compiler/src/project/path.trb` (200 Zeilen
    String-Bastelei). Kandidat für später: String-LITERALE passen sich an einen erwarteten `Path` an (Literal-Traits),
    Variablen müssten dann bewusst konvertiert werden.
  - Kurzes Design-Dokument wie `LINEAR.md`, dann Umsetzung.
  - **Entschieden (2026-09-21, Namenskollision):** `std/path` gehört den DATEIPFADEN. Das Kurven-Paket der
    Engine-Reihe (Bezier, Splines, Polylinien, Tessellierung) heißt `std/curve` - ein Wort, das nur eines bedeutet,
    und "path" liest jeder zuerst als Dateipfad. Das Design-Dokument `docs/PATH.md` entsteht jetzt schon parallel
    (neue Datei, stört keine laufende Runde), die Umsetzung bleibt hinter der Aufräumrunde `std/core`.
  - **Erledigt:** Design-Dokument `docs/PATH.md` (in `docs/internals/index.md` eingetragen, beide Doku-Gates grün).
    Die drei wichtigsten Entscheidungen: **`joined` läßt die Wurzel des Arguments fallen** statt ein `Result` zu
    antworten (`base.joined(x)` beginnt damit *immer* mit `base` - stärker als ein `Result`, und die ~25 Aufrufstellen
    im Compiler brauchen kein `?`; `resolved(inside:)` bleibt das fehlbare Member für fremde Eingaben). **`/` UND `\`
    trennen auf JEDER Plattform**, `.` fällt beim Bauen weg, `..` bleibt bis `normalized()` (`a/./b` ist immer `a/b`,
    `a/../b` nur ohne Symlink - eine Konstruktion rät nicht). **`Path` ist `Equals`/`Hash`/`Compare`, lexikalisch und
    überall case-sensitiv**; ob zwei Pfade dieselbe Datei meinen, fragt das Dateisystem.
    - **`fn open(path: Into<Path>)` geht heute NICHT**, und die Probe sagt genau warum: ein Trait als Parametertyp
      funktioniert (auch ein generisches, auch mit Coercion aus einem fremden `extend`) - gemessen auf Stage 0, im
      Typprüfer und als kompiliertes Binary. Nur `Into` selbst scheitert: `isConversionCall`
      (`compiler/src/semantics/checker/expression.trb:1959`) fängt JEDEN argumentlosen `into`-Aufruf ab und deutet ihn
      als `From` des Ziels, auch wenn der Empfänger selbst den Typ `Into<Path>` hat und `into` sein eigenes Member ist.
      Eine Bedingung im Checker. Bis dahin: `fn open(path: Path)` und `.into()` an der Aufrufstelle (kein `expect`).
    - Der ganze Typ samt aller Member ist als Probe gebaut worden: 38 Zeilen Ausgabe, Stage 0 und Binary identisch.
      In `std` fehlt dafür nur `String.lastIndexOf`.
  - **Entschieden (2026-09-21, zu den offenen Fragen von `docs/PATH.md`):** `Path` kommt NICHT ins Prelude
    (`use Path from "std/path"`, wie `std/fs`). `Root.Share` kommt gleich mit - ohne es würde `//server/share/x`
    FALSCH geparst statt gar nicht. Die Wide-Char-Runtime für Windows (`runtime/platform.c` geht heute als schmaler
    `char *` ans System: Nicht-ASCII-Pfade scheitern im Binary, Pfade über `MAX_PATH` gehen gar nicht - beides echte
    Abweichungen zu Stage 0) wird eine **eigene Runde vor Slice 2**, mit Gate-Programm. Der `isConversionCall`-Fix
    ist der laufenden kleinen Runde mitgegeben; `String.lastIndexOf` kommt in die Aufräumrunde `std/core`.
    **Deine Wahl (Geschmack):** `nameWithoutExtension` (so im Dokument) oder `stem`.
  - **Wird gelöst (mit den Slices):** `File.list` und `File.absolutePath` weichen zwischen Stage 0 und C-Runtime ab
    (lossy Namen bzw. Backslashes) - Slice 2 macht beide zu `Path`-Antworten mit EINER Form; `SandboxError` trägt
    keinen Pfad (Slice 3); `Process.run(workingDirectory:)` existiert noch nicht (Slice 3).
  - **Erledigt:** Wide-Char-Runtime für Windows (die eigene Runde vor Slice 2). Die Windows-Hälfte von
    `runtime/platform.c` ruft durchgehend die Wide-API (`_wfopen`, `_wmkdir`, `GetFileAttributesW`, `FindFirstFileW`,
    `CreateProcessW`, `_wgetenv`, `GetCurrentDirectoryW`, `DeleteFileW`/`RemoveDirectoryW`), zwei Helfer konvertieren an
    der Grenze (`torb_platform_wide`/`torb_platform_utf8`, mit `MB_ERR_INVALID_CHARS`/`WC_ERR_INVALID_CHARS`), und eine
    fehlgeschlagene Konvertierung ist dieselbe Antwort wie ein Name, den es nicht gibt - keine neue Fehlerart. Ein Pfad,
    dessen voll qualifizierte Form länger als `MAX_PATH - 13` Zeichen ist, geht als `\\?\C:\...` bzw.
    `\\?\UNC\server\share\...` hinüber (vorher durch `GetFullPathNameW` normalisiert, weil die Form nichts Relatives und
    kein `..` erlaubt), ein kürzerer in der schlichten Form: nicht immer, weil `\\?\` die Pfadauflösung des Systems ganz
    abschaltet (`..` und ein Punkt am Ende werden Namensteile, `NUL`/`CON` sind keine Geräte mehr, relativ geht nicht) -
    dann wäre jeder gewöhnliche Pfad einer, den diese Datei aufgelöst hat, und nicht der, den der Aufrufer geschrieben
    hat. `Process.arguments()` kommt aus `GetCommandLineW`/`CommandLineToArgvW`, weil das `argv` von `main` auf Windows
    die Codepage der Maschine ist. `File.absolutePath` hat in BEIDEN Implementierungen eine Form (Vorwärts-Schrägstriche,
    großer Laufwerksbuchstabe, kein `\\?\`): Stage 0 rechnet sie wie die C-Runtime (`absolute_path_of` in `natives.rs`;
    `std::path::absolute` kann sie nicht - es läßt `..` auf POSIX stehen und nimmt die Trenner der Plattform). Ein
    Verzeichniseintrag ohne UTF-8-Schreibweise ist in beiden ein `IoError` (`to_string_lossy` ist weg). Dazu vier
    Gate-Programme (`non-ascii-paths`, `long-paths`, `absolute-path-form`, `process-non-ascii-argument`), zehn C-Tests
    (`runtime/tests/platform_test.c`, 106 statt 96) und ein Abschnitt in `docs/BACKEND.md`.
    - **Gemessen, damit es nicht falsch im Kopf bleibt:** auf einer Maschine mit Codepage 1252 ist die schmale Umsetzung
      eine BIJEKTION (UTF-8-Bytes ↔ 1252-Zeichen), also fällt sie einem Programm, das seine Dateien selbst anlegt und
      selbst wieder liest, nicht auf - auch nicht über einen Kindprozess, weil `CreateProcessA` genauso verbogen hat.
      Falsch war sie trotzdem: auf der Platte stand `grÃ¼ÃŸe`, also scheitert jeder Name, der von AUSSEN kommt
      (`git checkout`, Explorer, ein anderes Programm), und auf einer DBCS-Codepage (932) ist die Abbildung
      verlustbehaftet. Hart gescheitert ist die alte Runtime an zwei Dingen, und beide sind gegen sie gemessen: Pfade
      über `MAX_PATH` (`long-paths` wird rot) und `argv` (`ü` kam als ein Byte 0xFC an, `日` als `?` - nicht mehr
      umkehrbar).
    - **Offen (bewußt nicht in dieser Runde):** `File.delete`/`File.rename` gibt es in `std/fs` nicht und
      `docs/PATH.md` listet sie auch nicht, deshalb legen die Gate-Programme unter `build/` an und räumen nichts weg -
      wie `files.trb` es schon tut. Die C-Tests räumen auf, weil die Plattformschicht dafür `torb_platform_remove` hat
      (nur `runtime/tests` ruft es, wie bei `torb_platform_set_environment_variable`). Die Konsolen-Ausgabe bleibt rohe
      UTF-8-Bytes: in eine Pipe ist das genau richtig (die Suite vergleicht Bytes), in einer Konsole mit Codepage 850
      sieht Nicht-ASCII falsch aus - der Fix wäre `WriteConsoleW`, wenn das Handle eine Konsole ist (`console.c`, kleine
      eigene Runde), nie eine globale Codepage-Umstellung.
    - **Erledigt:** Konsolenausgabe geht jetzt über `WriteConsoleW`/`ReadConsoleW`, wenn das Standard-Handle laut
      `GetConsoleMode` eine echte Konsole ist (einmal pro Stream geprüft und gecacht) - konvertiert über
      `torb_platform_wide`/`torb_platform_utf8`, in Stücken, die nie ein Surrogatpaar zerschneiden
      (`torb_console_chunk_length`, pur und ohne Konsole testbar). Eine Pipe oder eine Datei bekommt weiterhin genau
      die rohen Bytes, ungeprüftes UTF-8 auch (`torb_platform_wide` scheitert daran und ist der Fallback-Grund), und
      `SetConsoleOutputCP` läuft nach wie vor nie. Neun neue Tests in `runtime/tests/console_test.c`
      (118 Runtime-Tests), ein Absatz in `docs/BACKEND.md`. Offen, weil von hier aus nicht beobachtbar: eine echte
      Konsole zeigt jetzt tatsächlich `grüße 日本` statt Mojibake - das kann nur wer mit einem echten Konsolenfenster
      gegenprüfen (`chcp 850` und `torb build`/`cmd`).
      - **Nachgebessert beim Merge (2026-09-21):** `readLine` an der Konsole schrieb den Terminator bei einer
        Eingabe ohne Zeilenende genau hinter den vollen Puffer - eine Einheit bleibt jetzt frei.
      - **Wird gelöst (nach Schwanz 5, das gerade in `runtime/` arbeitet):** der Panic-Report schreibt mit
        `fprintf(stderr, ...)` an `console.c` vorbei, erscheint in einer Konsole also weiter in der Codepage; er geht
        dann durch denselben Weg wie `print`.
      - **Bitte einmal von dir prüfen (ich sehe keine echte Konsole):** in `cmd` `chcp 850`, dann ein mit
        `torb build` gebautes Programm mit `print "grüße 日本"` starten.


- (**Erledigt: Meilenstein 5.14 - zwei Implementierungen, ein beobachtbares Verhalten**, 2026-09-20)
  **Die Konformitäts-Suite vergleicht jetzt alles, was ein Programm beobachtbar tut, und nichts ist ausgenommen**:
  Standardausgabe, Standardfehler und Exit-Code aller **57 Gate-Programme** in `bootstrap/tests/native/` (plus eins in `stage-0-only/`), auf Stage 0
  und im kompilierten Binary, Byte für Byte. Die eine erlaubte Ausnahme ("ein Programm, das paniert, muss auf Stage 0
  nur *scheitern*") ist weg. `bootstrap/tests/native/README.md` ist der Vertrag: was verglichen wird, was nicht und
  warum, wie man ein Programm hinzufügt, und welches Programm welches Verhalten festnagelt.
  - **Ein Programm endet auf drei Arten, und Stage 0 sagt jetzt welche.** `Failure` trägt eine `FailureKind`: ein
    **Panic** druckt `panic: <message>` plus `  at <Pfad>:<Zeile>:<Spalte>` und endet mit **101**; ein **Top-Level-`?`**
    druckt `error: <Fehler über Show>` plus eine `  caused by:`-Zeile pro Glied von `cause()` und endet mit 1; ein
    **Fehler des Interpreters** (ein Name, den es nirgends gibt, eine Methode, die ein Wert nicht hat) behält Stage 0s
    eigenen Bericht mit den Aufrufen, durch die er kam, und endet mit 1. Die dritte Art hat im kompilierten Programm
    kein Gegenstück - der Typprüfer von Stufe 1 lehnt jedes Programm ab, das eine erreicht - und ohne sie hätte
    "Stage 0 soll sich anpassen" bedeutet, jeden Absturz des Compilers mit `panic:` und 101 zu melden oder die Frames
    zu verlieren. **Ein Panic druckt zwei Zeilen und nicht mehr**, wie das Release-Profil eines kompilierten Programms;
    `TORB_FRAMES=1` holt die Frames zurück.
  - **Ein Laufzeit-Ort ist der stabile Pfad**, `torbscript/compiler/src/ir/print.trb`: Paketname plus Datei unter dem
    Paketverzeichnis, genau was `pathsOfModules` des Lowerings interniert. Stage 0 hat keinen Workspace, also läuft der
    Loader bis zur nächsten `project.trb` hoch und liest ihr `name "..."` statisch (30 Zeilen in `program.rs`, pro
    Verzeichnis gecacht). Kein Arbeitsverzeichnis und keine Maschine kommt mehr in die Ausgabe - auch Windows'
    verbatim `\\?\` nicht mehr. Ein Problem beim *Laden* behält den Maschinenpfad, weil es für den Editor ist.
  - **Alle notierten Abweichungen sind geschlossen** - und die falsche Seite war nicht immer Stage 0:
    Panic-Format und Exit-Code (Stage 0), ``arithmetic overflow in `*` `` mit Operator (Stage 0),
    ``division by zero in `/` `` und `%` (Stage 0), Index/Slice/Text-Slice außerhalb (Stage 0), `expect` auf `None`
    und auf `Fail` samt der `std/`-Datei im Frame (Stage 0), `nan` überhaupt vergleichen (Stage 0),
    `Char`/`String`-Case-Mapping (**beide**), `Char.isDigit`/`isLetter`/`isWhitespace` (Stage 0), und
    **`sorted` ließ ein `nan` stehen, weil `List.sort` mit `<=` verglich (`std/collections`, echter Bug im
    kompilierten Build)**. Dazu: eine `project.trb` war überall ein gewöhnliches Modul, wo `test { input "." }` sie
    einsammelte, und wurde als Syntaxfehler gemeldet (Front-End).
  - **Entschieden (ich, zur Durchsicht):**
    1. **Auf einem Float ist jeder *Operator* IEEE-754, und nur `compare` ist die Totalordnung.** `<`, `<=`, `>`, `>=`
       kommen zu `==`: alle `false`, wenn auf einer Seite ein `nan` steht. Das ist, was die Intrinsic ohnehin emittiert
       und was C, Rust und Java alle tun, und ein Float-Vergleich bleibt verzweigungsfrei. Preis: ein Float ist der
       **eine** Typ, bei dem Operator und Member auseinandergehen - also ruft **alles, was ordnet, `compare`**, und
       `List.sort`, `minBy` und `maxBy` sind umgestellt.
    2. **Case-Mapping ist die *einfache* 1:1-Abbildung eines Codepoints, über ASCII und die Buchstaben von Latin-1.**
       Ein `Char` hält einen Codepoint, also ist `'ß'.toUpperCase()` gleich `'ß'`: Großschreibung ist `SS`, `'S'` wäre
       falsch, und ein `String` zurückzugeben würde den *Typ* des Ergebnisses vom Wert abhängig machen. `'ÿ'` zu `'Ÿ'`
       ist das eine Paar, das aus Latin-1 hinausreicht; `×` und `÷` sind Symbole. `String.toUpperCase` ist dieselbe
       Abbildung pro Zeichen, ein abgebildeter Text hat also genau so viele Bytes wie vorher. Volle Unicode-Tabellen
       bleiben Meilenstein 8. `runtime/text.c` und `bootstrap/crates/torb-interpreter/src/characters.rs` sind dieselben
       fünf Funktionen in zwei Sprachen und sagen es beide.
    3. **Ein Fehler des *Interpreters* ist eine eigene Art zu enden** (siehe oben), weder Panic noch Top-Level-`?`.
  - **`language.trb` typprüft wieder** (repariert, nicht ersetzt: ein 280-Zeilen-Programm, das Traits, Delegation,
    Patterns, Receiver-Closures und Wertsemantik in einem Lauf durchgeht, ist eine andere Art Test als 57 kleine).
    Nötig war: `.toList()` auf vier gedruckten Pipelines, `sort({ _ })` statt `sort()`, ein `fn` im Block zu einer
    Top-Level-Funktion mit Parametern (ein `fn` im Block ist keine Closure), eine Map per Schleife statt eines `toMap`,
    das es nicht gibt, eine Closure, die nicht mehr `var self` fängt, und die drei handgeschriebenen Operator-Traits
    gelöscht zugunsten der Prelude-Traits - darum stehen `Add`, `Subtract`, `Multiply`, `Divide`, `Remainder`, `Negate`
    und `Compare` jetzt in Stage 0s `prelude.trb`: `a + b` meint das `Add` der Prelude, und ein Skript, das ein
    eigenes Trait gleichen Namens deklariert, bekommt den Operator nicht.
  - **Zweites `check` im Gate** (`compiler/CONTRIBUTING.md`): `torb run ../compiler check tests/native tests/scripts`,
    61 Dateien. Beide Stage-0-Testverzeichnisse sind eigene Workspaces. Draußen bleiben nur `tests/parser-cases/`,
    `tests/lexer-cases/` (absichtliche Fehler) und `.vscode/.../samples/tokens.trb`, das kein Programm ist und nur
    parsen muss - steht jetzt in seinem eigenen Kopfkommentar.
  - **15 neue Gate-Programme** für Verhalten, das keines hatte (gesucht in den Referenzordnern unter `docs/language/`):
    `copies.trb`, `closure-captures.trb`, `integer-division.trb`, `sort-stability.trb`, `match-order.trb`,
    `character-case.trb`, `float-order.trb` und sieben Panics (`negate-overflow`, `remainder-by-zero`, `expect-none`,
    `expect-failure`, `slice-out-of-range`, `slice-reversed`, `text-slice-past-end`). Das fünfzehnte, `error-chain.trb`, hat eine
    **nicht notierte Abweichung gefunden**: CONCEPT zeigt die `  caused by:`-Zeilen eines Top-Level-`?`, Stage 0 schreibt
    sie jetzt, das Back-End schreibt nur die erste Zeile (`reportFailure` in `ir/lower/match.trb` sagt selbst, dass die
    Schleife über `cause()` noch fehlt - fremdes Agenten-Verzeichnis, darum nicht angefasst). Das Programm wartet in
    `bootstrap/tests/native/stage-0-only/`, wird dort auf Stage 0 gelaufen und verglichen, und zieht ein Verzeichnis
    hoch, sobald die Schleife existiert.
  - **Was noch abweichen darf** (in `docs/BACKEND.md`, Abschnitt "What 5.14 decided", mit Begründung):
    `Show` eines Funktionswerts (Stage 0 druckt `<function>`, das Back-End kann `show` eines `Closure(...)` noch nicht
    emittieren - beide Hälften sind echte Arbeit, und Stage 0 hat keine Typen, um `(Int64) => Int64` zu buchstabieren;
    **das ist der eine Punkt, der auf 6.3s Liste weiterwandert**), ein `Range` von etwas anderem als `Int` auf Stage 0,
    das `compare`-*Member* eines Tupels auf Stage 0 (kein gültiges Programm kann es sehen), ein `?`, das seinen Fehler
    über ein generiertes `From` konvertiert, `Process.run` eines fehlenden Programms unter POSIX (`popen`, bis 7.3),
    und `tls true` statt `tls(port == 8443)` in `dsl.trb`.
  - **Gates:** `cargo build --release`, `check ..` 280 Dateien "no problems", `check tests/native tests/scripts`
    62 Dateien, `check --statistics ..` 178847/178847 getypt und **0 deferred**, `canon --check` 0 von 344 Dateien,
    `docs check` 219 Seiten / 895 Snippets, `docs index --check` 24 Indizes, `cargo fmt --check`,
    `cargo clippy --all-targets -- -D warnings`, `sh runtime/build.sh` 96 Tests, `torb test ../compiler/tests`,
    volle `cargo test --release`.


- (**`std/linear` + `std/geometry`**, 2026-09-20) **Erledigt, im Zweig grün.** Entwurf `docs/LINEAR.md`, Trait `Real` in
  `std/number`, die beiden Pakete, 164 Tests unter `torb test`, drei native Gate-Programme byte-gleich mit Stage 0
  (`linear.trb`, `geometry.trb`, `grid-vectors.trb`), Referenzseiten `docs/standard-library/linear.md`+`geometry.md`.
  - **Was steht.** EIN generischer Typ je Breite (`Vector2<Scalar: Numeric = Float>`, `Vector3`, `Vector4`,
    `Matrix2/3/4`, `Quaternion`, `Angle`) plus `Fixed`; in `std/geometry` `Rectangle`, `Circle`, `Segment`, `Ray2`,
    `Triangle2`, `Polygon`, `Box`, `Sphere`, `Ray3`, `Plane`, `Triangle3`. Die Schichtung über bedingtes `extend`
    funktioniert vollständig - Checker, Interpreter und C-Back-End -, und `Vector2<Int>`, `Vector2<Float>` und
    `Vector2<Fixed>` sind eine Deklaration und drei Instanzen.
  - **`Fixed` ist implementiert, nicht nur entworfen: Q16.16 in einem `Int64`.** Q32.32 fiel raus, weil die
    Multiplikation ein 128-Bit-Zwischenergebnis braucht, das die Sprache nicht hat; `Wurzel` ist Newton auf Ganzzahlen,
    `sine`/`cosine`/`arcTangent2` sind CORDIC (16 Drehungen, je eine Addition und eine Division durch eine Zweierpotenz,
    Rundung zur nächsten Zahl statt Richtung Null - ohne das summieren sich 16 Abschneidungen zu einem sichtbaren
    Fehler). Genauigkeit ca. zwei Teile von 65536. **Keine Bit-Operationen**, weil Stage 0 `shiftedLeft` gar nicht hat -
    jede Schiebung ist als Division geschrieben, und genau deshalb laufen Tests und Gate-Programme überhaupt.
  - **Der eigentliche Gewinn ist eingetreten:** `Vector2<Fixed>`, `Rectangle<Fixed>`, `Quaternion<Fixed>` und jeder
    Schnitttest laufen unverändert, byte-gleich zwischen Interpreter und Binary. `Fixed` ist zurzeit auch die EINZIGE
    Trigonometrie, die der Interpreter ausführen kann (siehe unten).
  - **Meine Entscheidungen:** Matrix speichert ihre SPALTEN als Vektoren (`xAxis`...), nicht `Array<Scalar, 16>` -
    `Array` läuft in keinem Back-End und eine Spalte ist genau das, was eine Frage beantwortet; Matrix × Vektor heißt
    `applied(to:)` und nicht `*`; `Angle`-Wrapper JA (radiant innen, Grad an der Grenze); rechtshändig, Spaltenvektoren,
    `matrix * vector`, Winkel von der ersten zur zweiten Achse; `Rectangle`/`Box` halb-offen (Minimum drin, Maximum
    draußen - damit kachelt eine Reihe), `Circle`/`Sphere` geschlossen; y-oben/y-unten wird NICHT angenommen (kein
    `up`/`top`/`bottom` im ganzen Paket); kein SIMD, keine Swizzles, keine Projektionsmatrix, keine Farbe.
  - **Was Sprache/Compiler liefern müssen** - Abschnitt 12 von `docs/LINEAR.md`, nach Schmerz geordnet, jeweils mit
    Reproduktion. Die drei, die wirklich weh taten: (1) **ein Operator auf einem generischen Typ wird nicht gelowert**
    (`a + b` scheitert, `a.add(b)` nicht; Ursache in `ir/lower/call.trb`, `operandTypeOf` nimmt das Ziel der
    Implementierung statt den geschriebenen Operanden) - deshalb stehen in den Gate-Programmen Methoden; (2) **ein
    Zahlliteral in einem generischen Körper behält seinen alten Typ** (interner Fehler im Back-End bei `Float64`) -
    daraus fallen `Real.unit`, `Real.halved` und `zeroOf` als Behelf, sie verschwinden mit der Reparatur; (3) **Stage 0
    löst KEINEN Paketimport auf** - jeder Test und jedes Gate-Programm importiert Module von `std/` über den PFAD, und
    `std/geometry` erreicht `std/linear` so ebenfalls; deshalb liegt `zeroOf` als eine Zeile in beiden Paketen statt
    einmal in `std/number`.
  - **Daneben gefunden:** ein `const` eines generischen Typs wird nicht per Typargument instanziert; ein Default eines
    Typparameters wird für ein Mitglied eines konkreten `extend` nicht benutzt (`Vector2<Float>.zero` nötig statt
    `Vector2.zero`); Stage 0 kann zwei Instanzierungen eines `extend` nicht unterscheiden (deshalb trägt KEINE
    Konversion denselben Namen in zwei Instanzierungen); ein statisches Mitglied ist über einen Typparameter nicht
    erreichbar; `Float32` kann `Real` nicht tragen (keine Arithmetik im C-Back-End UND keine Konversion von `Float64`
    herunter); ein Trait kann kein `const` fordern (sonst trüge `Real` `pi`); ein `where` an einer Methode eines
    generischen `type` fügt nichts hinzu; zwei `Multiply`-Implementierungen an einem Typ sind unerreichbar.
  - **Offene Fragen an dich** stehen in Abschnitt 14 von `docs/LINEAR.md`: `isCloseTo` oder `isNear` (ich habe den
    bestehenden std-Namen genommen); `interpolated` oder `lerp`; `Segment` oder `Segment2`; sollen die vier
    Vektorkonstanten auf jeder Instanzierung bleiben (das ist die einzige Stelle, an der ein Interpreter-Programm den
    falschen Skalar liest); soll `Matrix4` eine allgemeine Inverse bekommen; `Quaternion.interpolated` nimmt den geraden
    Weg statt des Großkreises; gehört `std/linear` ins Prelude.
  - **Nächster Schritt laut Plan:** `Buffer<Item>`, dann `std/tensor` + `Dual`. `std/transform` ist der Ort für
    Koordinatenräume (euclids Phantom-Parameter - der beste Fund der Recherche, und in `std/linear` wäre er falsch),
    `std/collision` der Ort für Kontakte, `trait Bounds` und Broadphase; Abschnitt 13 von `docs/LINEAR.md` hat je einen
    Absatz zu `std/transform`, `std/collision`, `std/path`, `std/animation`, `std/color`.
  - **Entschieden (2026-09-21, zu den offenen Fragen - nur 1 ist Geschmack und bleibt bei dir):**
    1. `isCloseTo` bleibt vorerst (bestehender std-Name); `isNear` wäre eine mechanische Umbenennung über die ganze
       std - **deine Wahl**, sag Bescheid.
    2. `interpolated(toward:, by:)`, nicht `lerp` - volle Wörter.
    3. `Segment2`: `Ray2`/`Ray3` und `Triangle2`/`Triangle3` tragen die Ziffer, und für die Strecke gibt es kein
       eigenes Raumwort wie bei `Rectangle`/`Box` und `Circle`/`Sphere`.
    4. Die vier Vektorkonstanten bleiben auf jeder Instanzierung. Die Falle ist ein Fehler von Stage 0 (Lücke 6),
       keiner der API - die API richtet sich nicht nach dem Bootstrap-Interpreter.
    5. `Matrix4` bekommt die allgemeine Inverse; `inverseAffine` bleibt als der schnelle Weg daneben.
    6. `Quaternion.interpolated` bleibt der gerade Weg mit Renormalisierung; der Großkreis kommt als
       `interpolatedSpherically` mit `std/animation`, wo er gebraucht wird.
    7. `std/linear` kommt NICHT ins Prelude: das Prelude ist der Wortschatz jedes Programms, nicht der einer Domäne.
    - In `docs/LINEAR.md` Abschnitt 13 heißt das Kurven-Paket noch `std/path`; es heißt `std/curve` (siehe Eintrag
      `std/path`). Wird mit der Folgerunde korrigiert.
  - **Wird gelöst - Runde "generische Zahlen" im Back-End (nach Schwanz 4, weil beide `ir/lower` anfassen):**
    Lücken 1-4, 7, 11, 12 und die zwei kleinen aus 13: Operator auf generischem Typ, Zahlliteral im generischen
    Rumpf, `const`-Mitglied je Typargument, Parameter-Default bei konkretem `extend`, statisches Mitglied über einen
    Typparameter (`Scalar.zero()`), `where` an der Methode eines generischen Typs, zwei `Multiply` an einem Typ.
    Danach verschwinden `unit`/`halved`/`doubled`/`zeroOf` aus `Real`, die Gate-Programme schreiben `a + b`, und
    `Segment2`, die `Matrix4`-Inverse und `std/curve` im Text ziehen in derselben Runde mit. `Fixed.parse` wird in der
    Aufräumrunde `std/core` zu `TryFrom<String, NumberParseError>`.
    **Zurückgestellt:** Lücke 5/6 (Paket-Importe und Instanzierungen in Stage 0) - Stage 0 ist der Bootstrap, die
    Antwort ist die VM (7.x) bzw. `main.exe test`; Lücke 8 (`Float32`-Arithmetik), 9 (`Array` läuft nirgends - hängt
    an der kleinen Runde und dem Back-End), 10 (ein Trait fordert ein `const` - Kandidat, eigene Entscheidung).

- (`Default`, 2026-09-21) **Entschieden (mit dir besprochen, "Ok passt"): kein `default`-Schlüsselwort, kein
  `Default`-Trait.** Steht im Decision-Log von CONCEPT. C#s `default(T)` ist genullter Speicher am Konstruktor vorbei
  (bricht validierte Typen, ist `null` durch die Hintertür); Rusts `Default` bündelt fünf Bedeutungen, die bei uns je
  einen präzisen Namen haben: Feld-Defaults (`Config(port: 1)`), sichtbarer Ersatz (`value ?? 0`), das Literal als
  neutrales Element (`var result: Scalar = 0`), Feld-Default beim Decodieren, `None`.
  - **Wird gelöst:** `Map.getOrInsert(key, fallback: lazy Value)` in der Aufräumrunde `std/core` (das ist Rusts
    `entry().or_default()`, mit sichtbarem Ersatz). Das Literal im generischen Rumpf ist Lücke 2 der Runde
    "generische Zahlen".
  - **Kandidat, erst bei zwei konkreten Bedarfsstellen:** ein implizit abgeleiteter Fakt "ohne Argumente
    konstruierbar" (`Type()`), nach derselben Logik wie das abgeleitete `Encode`.

- **Erledigt (Checker-Runde: `Array`, Kleinigkeiten, Kohärenz, `TryInto`, 2026-09-20)** - alle Gates grün.
  - **`Array.of` ist weg**, und mit ihm `checkArrayOf`. Ein `Array` entsteht nur noch aus einem Listen-Literal (Anzahl
    gegen `Size` geprüft, Spread aus einem anderen `Array` addiert sich), aus `Array.filled(value)`, aus dem neuen
    `Array.generated { index => ... }` oder aus `Array.from(items)`. Die Fehlermeldung nennt beide Zahlen:
    "`Array<Int64, 4>` has 4 items, and this literal has 3".
  - **`Array` liegt jetzt in `std/core`** (`std/core/src/array.trb`), nicht mehr in `std/collections`; das Prelude
    exportiert es weiter, also ändert sich für kein Programm etwas. `Array` hat neu auch `Show` - vorher konnte man
    ein Array nicht ausgeben ("does not implement `Show`"), was jedes Doku-Beispiel auffiel.
  - **Ein `const`-Parameter, den nichts löst, hat eine eigene Notiz.** `Array.filled 0.0` ohne erwarteten Typ sagt
    jetzt "`Size` is a `const` parameter and is never inferred from an argument: write it in the type that is expected
    of this value" statt der Notiz über Closure-Parameter und Typargumente, die auf einen Wert nicht passt.
  - **`_` als Typargument ist nur als Kandidat notiert** (CONCEPT, Open Questions), nicht implementiert.
  - `Iterable.indexed()` liefert `(index: Int, item: Item)`; `std` liest `pair.index`/`pair.item`.
  - **`reportHere` behält jetzt eine Meldung pro Span UND Text.** Das hat sofort einen versteckten Befund gezeigt:
    `[head, ...Rest]` bekam nur die Schreibweise gemeldet, nicht dass die Bindung nie gelesen wird - beide stimmen,
    beide stehen jetzt da. Wo die zweite Meldung wirklich eine Folge ist, sagt die Stelle das selbst
    (`reportUnknownCase` fragt `hasDiagnosticAt`, damit "not a case in scope" nicht auf "write `limit`" draufkommt).
  - Gate-Fehlalarm behoben: ein Punkt in Backticks beendet keinen Satz mehr (`sentenceEnd` in
    `compiler/src/documentation/comments.trb`, mit Test).
  - Der `isPublic`-Schutz in `mutation.trb` war tatsächlich tot und ist weg: `public` macht aus einer Bindung eine
    `Declaration`, die eine `Declared`-Stelle bekommt und nicht die `Constant`-Stelle, die `isDeclaredVar` liest.
  - Neue Seite `docs/explanation/where-are-my-overloads.md`, von den vier Kontrastseiten und der Fehlerliste
    verlinkt. **Dabei gefunden:** zwei `draw`-Methoden in EINEM `type`-Körper gehen nicht (ein Namensraum pro Typ) -
    ein `extend` pro Trait-Instanz ist die Form, genau wie `std/core` es mit `From` macht.
  - **Kohärenz zählt jetzt die Typargumente des Traits.** `extend Float64 with From<Celsius>` ist im Paket von
    `Celsius` erlaubt: so konvertiert ein Paket seinen eigenen Typ in einen fremden. Nur die oberste Ebene eines
    Arguments zählt und nur ein benannter Typ (`From<List<Celsius>>` gilt nicht) - die strengere Lesart von Rusts
    RFC 2451. Ein Blanket behält die alte Regel.
  - **Ein Trait mit Blanket-Implementierung von Hand zu schreiben hat eine eigene Meldung**, aus dem `where` des
    Blankets gebaut: "`Into` comes from `From` for every type" mit "Write `extend Float64 with From<Celsius>`: the
    blanket implementation in `std/core` makes `Celsius.into()` out of that". Nur wenn das `where` EIN Trait-Bound
    auf einem Parameter ist; sonst bleibt die alte Overlap-Meldung.
  - **`TryInto<Target, Failure>` ist da**, mit Blanket über `TryFrom`, im Prelude. `tryInto()` liest Ziel UND Fehler
    aus dem erwarteten `Result`. **Was es nicht erreicht: ein `?` auf dem Aufruf** - `tryType` inferiert seinen
    Operanden ohne Erwartung, also hat `const small: Int8 = wide.tryInto()?` kein `Result` zum Lesen. Das sauber zu
    machen heißt, eine Erwartung durch `?` zu schieben und den Fehlertyp aus der umgebenden Funktion zu holen - eine
    Änderung an der Inferenz, nicht an einem Aufruf. Der Fall sagt es deshalb klar: "The target of `tryInto()` is not
    known here", mit `Target.tryFrom(value)` in der Notiz. **Frage an dich: soll das eine eigene kleine Runde werden?**
  - Nachgesehen: **nichts in `std/`, `compiler/` oder `examples/` implementiert `TryFrom<String, _>`.** Kein Blanket,
    das jedes `From` zu einem `TryFrom` mit `Never` macht - es würde jedes handgeschriebene `TryFrom` überlappen.
  - **Noch gefunden, nicht behoben (zwei Meldungen, die fehlen):**
    1. `Array.of 1, 2, 3` sagt heute nicht "`Array` has no member `of`", sondern "Cannot infer `Item` of `Array`".
       Grund: `reportUnknownMember` schweigt, wenn der Empfänger noch eine offene Variable enthält - bei einem
       STATISCHEN Zugriff auf einen generischen Typnamen sind die Variablen aber vom Checker selbst gemacht, nicht vom
       Schreiber. `List.nothingLikeThis 1, 2` hat dasselbe Loch, `String.nothingLikeThis` (nicht generisch) meldet
       richtig. Ein echter Fehlalarm-in-die-andere-Richtung, aber die Unterscheidung "statischer Zugriff" gehört in
       eine eigene Runde.
    2. `names.map(String.toUpperCase)` TYPPRÜFT, läuft aber auf Stage 0 nicht ("does not know
       `String.toUpperCase`"). Deshalb steht es NICHT in der neuen Erklärseite. Ebenso: `into()` wird von Stage 0
       überhaupt nicht aufgelöst ("a `Celsius` has no method `into`"), auch nicht für eigene Typen - also war für
       `tryInto()` in `bootstrap/` nichts zu tun.
  - Im Back-End nur die Manifest-Zeile: `Array.generated` steht dort wie `Array.filled` als `planned` (5.9b),
    `Array.of` ist raus. `Adaptation.Convert` für `tryInto()` wird von der Lowering wie die für `into()` behandelt,
    also mit einem sauberen "noch nicht unterstützt" und nicht mit falschem C.
  - **`into()` auf einem Empfänger vom Trait-Typ `Into<Target>` war kaputt und ist behoben.** `isConversionCall` hat
    JEDEN argumentlosen Aufruf von `into` mit erwartetem Typ abgefangen, ohne zu fragen, ob der Empfänger die
    Methode selbst schon hat - `fn open(path: Into<Path>) { const target: Path = path.into() }` meldete
    "`Into<Path>` does not convert into `Path`". Neue Regel, und sie ist auch die ehrlichere: **`into()` ist die
    `From` des Ziels nur dort, wo der Typ des Empfängers die Methode NICHT hat.** Hat er sie (ein Wert vom
    Trait-Typ `Into<Target>` hat sie, es ist die eine Pflichtmethode), ist es ein gewöhnlicher dynamischer Aufruf
    und geht durch `checkCall`. Dasselbe für `tryInto` auf einem `TryInto<Target, Failure>`. Drei Tests in
    `conversions.test.trb`, eine Regel auf `docs/language/types/conversions.md`.
  - **Kein natives Gate-Programm dafür, und der Grund ist ein Back-End-Befund.** `torb run ../compiler build` sagt
    zu `path.into()` bei `path: Into<Path>`: "not supported by the back end yet: `into`, a member the back end
    cannot build an instance of" (`compiler/src/ir/witness.trb:169`, `instanceFor` gibt `None`). Ein
    HANDGESCHRIEBENES `trait Convert<Target>` mit direkter Implementierung baut dagegen sauber durch - der
    Unterschied ist also das BLANKET: der Tabelleneintrag für `into` zeigt auf den Blanket-Körper, und der braucht
    im Eintrag den inneren Zeugen `Target: From<Source>`. Das liegt in `ir/` und damit nicht in meinem Bereich;
    Reproduktion: die drei Typen aus dem Test oben als Programm unter `bootstrap/tests/native/`. Stage 0 löst
    `into()` ohnehin nicht auf, ein Konformitätsprogramm braucht also beide Seiten.
  - **Zwei weitere Löcher, bestätigt, nicht behoben:** (1) `fn open<Source: Into<Path>>(path: Source): Path {
    path.into() }` typprüft und erreicht keines der beiden Back-Ends. (2) Ein `where`, dessen SUBJEKT ein konkreter
    Typ ist, wird nicht beachtet: `fn open<Source>(path: Source): Path where Path: From<Source> { Path.from path }`
    meldet "`Path` has no member `from`".
  - **Gates** (nach dem Merge von master mit 5.14): `cargo build --release`, `check ..` 280 Dateien "no problems",
    `check tests/native tests/scripts` 62 Dateien "no problems", `check --statistics ..` 180021/180021 getypt und
    **0 deferred**, `torb test ../compiler/tests` **1469 passed, 0 failed** (55 Dateien), `canon --check` 0 von 344
    Dateien, `docs check` 220 Seiten / 911 Snippets, `docs index --check` 24 Indizes,
    `docs source ../std/core ../std/collections ../std/iteration ../compiler/src/semantics ../compiler/src/documentation`
    75 Dateien / 904 Deklarationen / 50 Beispiele "no problems", `cargo fmt --check`, `cargo clippy --all-targets`,
    volle `cargo test --release`. Der Skill ist neu erzeugt und nach `.claude/skills/torbscript/` kopiert.
  - **Nicht gelaufen: der Fixpunkt.** Er steht nicht auf meiner Gate-Liste, aber `std/` hat sich geändert (`Array`
    umgezogen, `Show` dazu, `TryInto` mit Blanket) - wer als Nächstes am Back-End ist, sollte
    `cargo test --release --test fixpoint -- --ignored` einmal laufen lassen. Ein Blanket über JEDEN Typ ist genau
    die Form, die in BACKEND.md einmal die Instanzliste zum Wachsen gebracht hat (`Iterable.indexed`); hier steht in
    keinem Repository-Programm ein `tryInto()`, also erwarte ich nichts - geprüft ist es nicht.

- (**Erledigt: der unsterbliche gezählte Static, `Show` von allem Zusammengesetzten, und Plannummern raus aus den
  Meldungen**, 2026-09-21)
  - **Ein Modul-`const`, dessen Wert keine statischen Daten sind, wird genau einmal gebaut.** 6.3 hatte die Kosten
    gemessen (35 Tupel pro Interpunktionszeichen jeder Datei, die der Compiler liest) und die Form benannt; das ist, was
    die Form geworden ist. `FunctionKind.ConstantCell(initializer)` ist eine Funktion mit Signatur und **ohne Blöcke**:
    ein Lesen des `const` ist ein gewöhnlicher `Call` darauf, und die Zelle plus das Flag "ist gebaut" gehören dem
    Back-End (in C zwei `static` neben der Funktion, in der VM ein Slot des Modul-Frames). Der Initialisierer ist eine
    gewöhnliche Funktion ohne Parameter.
    - **Nicht read-only-Daten**, wie "eine Listen-Storage mit unsterblichem Zähler" gewesen wäre: ein `ExpressionNode`-Baum
      ließe sich als `static const struct` buchstabieren, aber `"a" + "b"` ist ein Aufruf von `Add.add` und ein
      Listenliteral ist `torb_list_with_capacity` plus ein `add` pro Element. Ein Mechanismus, der jede Form abdeckt, ist
      besser als zwei, die je eine abdecken.
    - **Beim ersten Lesen gebaut, und genau das hält das Versprechen.** Die Sprache sagt: es gibt **niemals** eine
      Modul-Initialisierung. Ein faul gebauter Unsterblicher hält das exakt: vor `main` läuft nichts, die Reihenfolge der
      Lesezugriffe entscheidet nichts, und ein `const`, das kein Pfad liest, wird nie gebaut. Unbeobachtbar ist es, weil
      die Reinheitsregel des Checkers für ein Modul-`const` gilt - Literale, Operatoren der Zahlentypen, Interpolation,
      Sammlungsliterale, Konstruktoraufrufe, andere Konstanten, **nie** ein Funktionsaufruf. In einer Entry- oder
      Testdatei ist ein Top-Level-`const` eine **lokale Variable der Entry-Funktion** und kommt hier gar nicht an - das
      schließt das eine Loch, das die letzte Runde notiert hatte.
    - **Unsterblich durch Konstruktion, damit das Leak-Gate exakt bleibt.**
      `torb_begin_immortal()`/`torb_end_immortal()` öffnen eine Region um den Aufruf des Initialisierers, und **jeder**
      Block, den `torb_allocate` darin herausgibt, wird mit `TORB_IMMORTAL_COUNT` geboren: retain und release sind
      No-Ops, `torb_make_unique` kopiert, und keiner wird je freigegeben. Die Region ist es, die den *ganzen Graphen*
      unsterblich macht - die Listen-Storage, die Texte darin, die Boxen eines Case -, wo ein Markieren nur des obersten
      Blocks dessen Kinder im Live-Zähler gelassen hätte. Die Temporaries, die der Initialisierer unterwegs macht, werden
      mit unsterblich; das ist eine feste Zahl Blöcke pro Programm und kein wachsendes Leck.
    - **Der Zähler wird geteilt, nicht das Gate geschwächt:** `torb_report_leaks` schreibt `live blocks at exit: 0`
      **und** `immortal blocks at exit: N`, `native.rs` prüft beide Zeilen, und `live blocks` heißt weiter "alles, was
      gezählt wurde, wurde freigegeben".
    - **Ein Body, der in einem anderen Body gelowert wird, ist jetzt ein Mechanismus.** Der Initialisierer wird dort
      gelowert, wo das `const` zuerst gelesen wird, und nicht über die Worklist: ein Konstrukt, das das Back-End noch
      nicht übersetzt, muss den Body abbrechen, der das `const` *liest*, und ein später abgearbeiteter Worklist-Eintrag
      erreicht den nicht mehr. Das ist die Form, die ein Closure-Body schon hatte, also sind
      `savedFrame`/`enterBody`/`restoreFrame` jetzt `public` und tragen Modul, Substitution und `capturedVariables` mit.
    - **Der Workaround im Lexer ist raus**, gemessen auf Stage 2, `check --timings ..`, je drei Läufe: mit dem Feld
      1790/2023/2020 ms für "lexing, parsing and the module graph", ohne 1787/1963/1949 ms; alle Pässe 7430/7514/7432
      gegen 6928/7492/7247 ms; `program.c` 57270499 gegen 57270002 Bytes. Der direkte Zugriff ist **nie langsamer**, und
      beide Zahlen liegen im Rauschen der anderen - genau der Punkt: der allgemeine Mechanismus macht den Workaround
      unnötig, also entscheidet, dass ein Feld, ein Initialisierer und ein Kommentar darüber weg sind.
    - Gate: `bootstrap/tests/native/constants.trb` (Konstanten jeder Form, eine Tabelle von Tupeln, eine Liste aus
      anderen Konstanten, Lesen in einer Schleife, eine **mutierte Kopie**), byte-gleich mit Stage 0, `live blocks 0`,
      13 unsterbliche. 98 Runtime-Tests (drei neue in `memory_test.c`).
  - **Ein Trait-Member wird nach Name *und Form* gesucht, und ein Name allein reicht nicht.** Das
    "a generated member without a receiver" bei `std/core/src/convert.trb:113` ging nie um `showNested`: `WellKnown` des
    Compilers hat ein **Feld** `show: SymbolId?`, `providerOf` des Checkers antwortet ein Feld unter dem Namen des
    Members, und das Back-End nahm es als Provider von `Show.show` - dessen generierte Instanz dann keinen Receiver
    hatte, weil die Signatur eines Feldes kein `self` nimmt. Ein Finding verweigerte den ganzen Build und mit ihm **jedes
    `Show` eines zusammengesetzten Werts der Compiler-eigenen Typen**: eine Probe, die einen `Checker` zeigt, lowerte
    7951 von 7952 Funktionen und lowert jetzt 7965 von 7965. `declaredMemberOf` stellt die Frage, die die
    Anforderungsprüfung des Checkers selbst stellt (`compareSignatures`: `required.takesSelf == given.takesSelf`) - für
    eine **abgeleitete** Implementierung läuft die Prüfung gar nicht, darum hat es vorher niemand gesehen.
    Gate: `show-compound.trb`. Eine neue notierte Abweichung: auf Stage 0 liest `value.show()` bei einem Wert mit Feld
    `show` das **Feld** (kein Typchecker), also zeigt das Gate so einen Wert nur über `print`.
  - **Keine Plannummer erreicht mehr eine Meldung, die ein Nutzer liest.** Findings lasen
    `not supported by the back end yet: a quoted expression (milestone 5.11)`. Welche Teil-Meilenstein-Nummer ein
    Konstrukt baut, ist eine Tatsache über den Plan dieses Repositories und nicht über das Programm vor dem Leser, und
    ein Plan, der sich verschiebt, lässt die Nummer falsch stehen. `unsupportedMessage` in `ir/unsupported.trb` ist die
    **eine** Stelle, die den Satz formt - `<das Konstrukt> is not supported by the native back end yet` - und beide
    Hälften gehen durch sie: das `reportUnsupported` des Lowerings für ein Konstrukt des Programms, das des Emitters für
    eines der IR. `torb build` schreibt die Findings des Emitters als `error: <Meldung>`-Zeilen wie die des Lowerings,
    statt einer Kopfzeile plus eingerückter Liste, die dasselbe zweimal sagte. Das Manifest behält
    `NativeState.Planned` mit seinem Meilenstein als **internen Schlüssel** - die Tabelle muss sagen, welche Einträge
    noch nicht geschrieben sind -, und er kommt nirgends heraus: ein geplanter Native liest
    `` `X`, which the runtime does not provide yet ``. 16 gepinnte Strings der Lowering-Tests und zwei
    Dokumentationsseiten mit Exact-Match-Skripten neu gepinnt.
  - **Entschieden (Koordinator, notiert in BACKEND 5.11 und auf 5.14s Liste):** ein fehlschlagendes `assert` zeigt
    nativ eine Capture, die ein Skalar ist (`Int*`/`UInt*`/`Float*`, `Bool`, `String`, `Char`), **nach Wert**, und jede
    andere nach **Name und Typ** (`found: LoweredProgram`). Das hält die Instanzkosten pro gefangenem Typ (1731
    Instanzen für einen `Checker`) und das Problem "ein nicht zeigbarer Typ verweigert den ganzen Test-Build" aus dem
    Testbinary. Volle Werte kommen mit `docs/ENCODING.md`.
  - **Nicht geschafft, mit Stand:** 5.11 selbst (quotierte Ausdrücke, `test`/`group` als Runtime-Funktionen,
    `main.exe test ../compiler/tests` als **ein** Binary für alle 55 Dateien), die kurzen C-Symbole (6.3s größter Hebel,
    62,5% der Datei), und die zwei Punkte aus 5.14: die `cause()`-Kette eines Top-Level-`?` nativ und `Show` eines
    Funktionswerts. Die Entwürfe dafür stehen unverändert in BACKEND ("What 5.11 needs" und 6.3s Liste); der
    unsterbliche Static, den 5.11 für den statischen `ExpressionNode`-Baum braucht, ist jetzt da.
  - **Gates:** `cargo build --release`, `check ..` 281 Dateien "no problems", `check tests/native tests/scripts`
    63 Dateien, `check --statistics ..` 0 deferred, `canon --check` 0 von 348 Dateien, `docs check` 219 Seiten,
    `docs index --check` 24 Indizes, `docs source` über `ir`/`backend`/`cli`/`std/test`/`std/expression` 43 Dateien
    "no problems", `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, `sh runtime/build.sh` 98 Tests,
    `torb test ../compiler/tests` 1453 Tests, volle `cargo test --release`, und **der Fixpoint hält**: Stage 1 und
    Stage 2 sind sich über 57289851 Bytes C einig (250,1 s / 108,1 s), Stage 3 emittiert sie noch einmal (22,0 s).

- **Erledigt (5.11, erste Hälfte: `assert` wird gelowert, `test`/`group` sind Runtime-Funktionen, 2026-09-21)** - alle
  gelaufenen Gates grün. Die zweite Hälfte (`torb test` aus einem Binary) steht unten unter "Nicht geschafft".
  - **Die 58 Findings `a quoted expression` in `compiler/tests/` sind 0.** `torb ir --statistics ../compiler/tests` lowert
    jetzt **12318 von 12319 Funktionen**; das eine, das bleibt, ist `a list pattern inside another pattern` in
    `parser.test.trb:178` und hat mit Quotierungen nichts zu tun (notiert, nicht angefasst).
  - **`assert` wird vom Lowering gebaut, und die Quotierung dahinter wird nie als Wert erzeugt.**
    `assert(condition)` braucht drei Dinge - ob die Bedingung hielt, was der Leser geschrieben hat, und was die Bedingung
    aus ihrer Umgebung las - und ein `Expression<Bool>`-Wert trägt keins davon billiger, als das Lowering es ohnehin hat:
    die Bedingung ist das gewöhnliche `Bool`, die Quelle sind die Bytes ihrer Span, die Captures stehen in
    `Quotation.captures`. Der Rumpf, den `std/expression` schreibt, ist der, der **nicht** gelowert werden kann: er reicht
    jede Capture als `Encode` weiter und fragt `describe` nach dem Text - eine `Encode`-Implementierung pro gefangenem Typ
    im Binary, und ein einziger nicht zeigbarer Typ verweigert den ganzen Build. Das ist genau der Preis, den 2801
    Assertions über die Compiler-eigenen Typen bedeutet hätten. Neu: `compiler/src/ir/lower/quote.trb`.
  - **Was ein fehlgeschlagenes `assert` zeigt, ist *eine* Funktion** (`describedCapture`), und das ist die Naht, auf der
    `EncodedValue` später landet. Heute: ein **Skalar** (jeder Integer-, jeder Float-Typ, `Bool`, `Char`, `String`) als
    `left = the Int 1` - zeichengleich mit der Beschreibung, die der Interpreter selbst schreibt -, alles andere als
    `found: Point`, Name und Typ. Das Wort nach `the` ist der Name der *Art* von Wert und nicht der des Typs (`Int` für
    ein `Int64`), weil das die Vokabel von Stage 0 ist und der Text verglichen wird.
  - **Stage 0s `assert` paniked jetzt**, statt als *Interpreter* zu scheitern (`error:`, Code 1). Das war eine Art zu
    enden, zu der ein kompiliertes Programm kein Gegenstück hat, also war ein fehlschlagendes `assert` vorher überhaupt
    nicht vergleichbar. Jetzt schreiben beide `panic: Assertion failed: ...`, dieselbe Stelle und 101.
  - **`test` und `group` sind Funktionen der Runtime** (`runtime/test.c`), weil die Runtime eine Closure aufrufen kann.
    Was darüber hinaus fehlte, war ein **Wiedereinstiegspunkt**: `torb_begin_recovery` in `runtime/panic.c` lässt
    `torb_finish_panic` eine `torb_recovery` füllen und in den Frame springen, der den Test ausführt, statt nach stderr zu
    schreiben und das Programm zu beenden. Der Punkt wird vor dem Sprung abgeräumt, also ist eine Panik *während* der
    Meldung eine gewöhnliche Panik. Der Report - `  ok      <gruppe> > <name>` und die vier Zeilen eines Fehlschlags -
    steht an *einer* Stelle, aus demselben Grund, aus dem `print` seine Teile in der Runtime zusammenfügt.
  - **Ein Lauf, der eine Panik auffängt, leakt, und das Gate sagt es, statt gelockert zu werden.** Eine aufgefangene Panik
    führt auf dem Weg hinaus nichts aus - kein Release, kein `Close`, kein Destruktor -, genau wie eine gewöhnliche Panik.
    `bootstrap/tests/native/test-failure.trb` meldet `live blocks at exit: 1` und trägt eine `.leaks`-Datei neben sich,
    die diesen Satz enthält; der Runner liest sie und lässt das Leak-Gate für genau dieses Programm aus. README der Suite
    und BACKEND sagen beides.
  - **Drei neue Gate-Programme.** `tests.trb` (eine Zeile pro Test, Gruppennamen davor, Gruppen schachteln) und
    `test-failure.trb` (ein Test scheitert, wenn sein Rumpf paniked: Name, Meldung eingerückt darunter, Stelle, und der
    nächste Test läuft) sind byte-gleich auf beiden Seiten. Für die *bewusste* Abweichung gibt es ein neues
    Unterverzeichnis `bootstrap/tests/native/binary-only/` - das Gegenstück zu `stage-0-only/` -, in dem ein Programm
    liegt, wenn die beiden Implementierungen absichtlich verschieden antworten: `assert-compound-capture.trb` wird gebaut
    und als Binary allein geprüft, und README und BACKEND sagen, was abweicht und wann es zugeht.
  - **Nicht geschafft, mit exaktem Stand:** (1) ein `Expression<Value>` als **Wert** - der statische `ExpressionNode`-Baum,
    `value()`, `captures()`; die beiden Einträge sind weiter `.Planned("5.11")`, und eine Quotierung, die nicht das
    Argument von `assert` ist, ist weiter `a quoted expression`. Der Entwurf steht unverändert in BACKEND ("What 5.11
    needs"), und der unsterbliche Static, den der Baum braucht, ist da. (2) `torb test` aus **einem** Binary über alle 55
    Dateien: eine kompilierte Testdatei läuft für sich allein schon nativ und schreibt, was Stage 0 schreibt - was fehlt,
    ist der Treiber (eine erzeugte Entry, die alle Module in Pfadreihenfolge aufruft, `emitProgram` mit mehr als einem
    Entry-Namen, und die Zähler der Summenzeile neben `test` in `runtime/`). (3) Damit auch die beiden 5.14-Punkte
    (`cause()`-Kette, `Show` eines Funktionswerts), die erst danach drankommen sollten.
  - **Gates:** `cargo build --release`, `check ..` 315 Dateien "no problems", `check tests/native tests/scripts`
    73 Dateien, `check --statistics ..` 195546 von 195546 Ausdrücken, 0 deferred, `canon --check` 0 von 383 Dateien,
    `docs check` 222 Seiten, `docs index --check` 24 Indizes, `docs source` über
    `ir`/`backend`/`cli`/`std/test`/`std/expression` 45 Dateien "no problems", `cargo fmt --check`,
    `cargo clippy --all-targets -- -D warnings`, `sh runtime/build.sh` 108 Tests, `torb test ../compiler/tests` **1469**,
    `torb test ../std/linear/tests` 93, `torb test ../std/geometry/tests` 71,
    `cargo test --release --test native` (die ganze Konformitätssuite, drei Tests, 1688,6 s) grün, und
    **der Fixpoint hält**: Stage 1 und Stage 2 sind sich über 57455864 Bytes C einig (279,2 s / 106,2 s),
    Stage 3 emittiert sie noch einmal (21,9 s). Nicht gelaufen: die volle `cargo test --release`.
