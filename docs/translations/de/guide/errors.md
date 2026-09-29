---
title: Fehler
summary: Eine Funktion, die scheitern kann, gibt ein Result zurück, ein Wert, der fehlen kann, ist eine Option, und der Aufrufer behandelt beides mit match, dem Fragezeichen oder einem Ersatzwert.
kind: guide
status: stable
order: 80
prerequisites:
  - traits.md
translates: 6223a690dbb3
---

Es gibt kein `null` und keine Exceptions. Ein Wert, der fehlen kann, und ein Aufruf, der scheitern kann, sagen es
beide in ihrem Typ, und der Compiler lässt den Aufrufer damit umgehen.

## Ziel

Am Ende dieser Seite kannst du eine Funktion schreiben, die scheitern kann, behandeln, was sie zurückgibt, und einen
Fehlschlag mit `?` weiterreichen.

## Eine Funktion, die scheitern kann

```trb run
type ConfigError {
  case Missing(key: String)
  case Invalid(key: String, reason: String)
}

fn readPort(settings: Map<String, String>): Result<Int, ConfigError> {
  const raw = settings.get("port").okOr(ConfigError.Missing("port"))?
  const port = Int.tryFrom(raw).mapError({ _ => ConfigError.Invalid "port", "not a number" })?
  if port < 1 || port > 65535 {
    return Fail ConfigError.Invalid("port", "out of range")
  }
  port
}

match readPort(["port": "80a"]) {
  Ok(port) => print "port {port}"
  Fail(.Missing(key)) => print "{key} is missing"
  Fail(.Invalid(key, reason)) => print "{key} is invalid: {reason}"
}
// prints port is invalid: not a number
```

`Result<Int, ConfigError>` ist entweder `Ok` mit einem `Int` oder `Fail` mit einem `ConfigError`. Ein `match` behandelt
beides, so wie es die Fälle jedes Typs behandelt. `settings.get` gibt ein `String?` zurück, und `okOr` macht aus einem
fehlenden Wert ein `Fail`. Der Rumpf endet mit `port`, nicht mit `Ok(port)`: Der Wert wird für dich eingepackt.

## Einen Fehlschlag mit ? weiterreichen

`?` nach einem Aufruf nimmt den Wert aus einem `Ok`. Bei einem `Fail` gibt es dieses `Fail` sofort aus der Funktion
zurück. Das erste `readPort` benutzt es zweimal. Unterscheiden sich die Fehlertypen, wandelt `?` den Fehler um,
sofern der Zieltyp sagt, wie:

```trb run
type ConfigError {
  case Missing(key: String)
}

type AppError {
  case Config(cause: ConfigError)
  case Startup(message: String)
}

fn readPort(settings: Map<String, String>): Result<Int, ConfigError> {
  if settings.isEmpty() {
    return Fail ConfigError.Missing("port")
  }
  8080
}

fn start(settings: Map<String, String>): Result<Void, AppError> {
  const port = readPort(settings)?
  print "listening on {port}"
  Ok void
}

match start([:]) {
  Ok(_) => print "started"
  Fail(error) => print "failed: {error}"
}
// prints failed: Config(cause: Missing(key: "port"))
```

`AppError.Config` ist der eine Fall, der einen `ConfigError` hält, also packt `?` einen `ConfigError` von selbst
hinein.

## Ein Wert, der fehlen kann

```trb run
const ages = ["Ada": 36]
const age = ages.get("Grace") ?? 0
print age
print ages.get("Ada")
// prints 0
// prints Some(36)
```

`Int?` ist kurz für `Option<Int>`: entweder `Some` mit einem Wert oder `None`. `??` gibt einen Ersatz für `None`, und
`?.` greift in den Wert, wenn es einen gibt: `findUser(2)?.name`.

## Ein panic ist für Bugs

```trb run
fn percentageOf(part: Int, total: Int): Int {
  if total == 0 {
    panic "total must not be zero"
  }
  part * 100 / total
}

print percentageOf(1, 4)
// prints 25
```

`panic` beendet das Programm mit Exit-Code 101, und nichts kann es abfangen. Nimm es für einen Zustand, der unmöglich
sein sollte. Eine schlechte Eingabe ist ein erwarteter Fehlschlag, und ein erwarteter Fehlschlag ist ein `Fail`.

## Weiter

- [Kollektionen und Pipelines](collections-and-pipelines.md) - Listen, Maps und Mengen, und wie du sie durchgehst.
- [Result](../language/errors/result.md) - alles, was du mit `Ok` und `Fail` machen kannst.
- [The question mark operator](../language/errors/question-mark.md) - die genaue Regel, und wann umgewandelt wird.
