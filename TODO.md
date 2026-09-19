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
