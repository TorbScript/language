---
title: Fehler
summary: Wie eine Funktion mit Result sagt, dass sie scheitern kann, und wie ein Aufrufer das mit match oder dem Fragezeichen-Operator behandelt.
kind: guide
status: stable
order: 70
prerequisites:
  - traits.md
translates: 151f894a57e9
---

Es gibt kein `null` und keine Exceptions. Abwesenheit ist ein Wert, `Option<Value>`, und Scheitern ist ebenfalls ein
Wert, `Result<Value, Failure>`. Diese Seite schreibt eine Funktion, die scheitern kann, und zwei Wege, mit dem
umzugehen, was zurückkommt.

## Ziel

Am Ende dieser Seite kannst du eine Funktion schreiben, die `Result` zurückgibt, sie mit `match` behandeln und das
mit dem Fragezeichen-Operator verkürzen.

## Eine Funktion, die scheitern kann

```trb
type ConfigError {
  case Missing(key: String)
  case Invalid(key: String, reason: String)
}

fn readPort(settings: Map<String, String>): Result<Int, ConfigError> {
  const raw = settings.get("port").okOr(ConfigError.Missing("port"))?
  const port = Int.tryFrom(raw).mapError { ConfigError.Invalid "port", "not a number" }?
  if port < 1 || port > 65535 {
    return Fail ConfigError.Invalid("port", "out of range")
  }
  port
}

match readPort(["port": "80a"]) {
  Ok(port) => print "Port {port}"
  Fail(.Missing(key)) => print "{key} is missing"
  Fail(.Invalid(key, reason)) => print "{key} is invalid: {reason}"
}
```

Die zwei Fälle von [`Result`](../language/errors/result.md) sind `Ok` und `Fail`, und die Prelude importiert sie
unqualifiziert, sodass ein Pattern `Ok(port)` schreibt statt `Result.Ok(port)`. `match` muss beide abdecken, genau
wie es jeden Fall jedes anderen Typs mit Fällen abdecken muss.

## Der Fragezeichen-Operator

`?` allein am Ende einer Zeile entpackt ein `Ok` und gibt sofort das `Fail` aus der umgebenden Funktion zurück – genau
das benutzt `readPort` oben schon zweimal, einmal auf einer `Option`, die mit `okOr` in ein `Result` verwandelt wurde,
einmal auf dem `Result`, das `Int.tryFrom` liefert.

```trb
type AppError {
  case Config(cause: ConfigError)
  case Startup(message: String)
}

fn start(settings: Map<String, String>): Result<Void, AppError> {
  const port = readPort(settings)?
  print "Listening on {port}"
  Ok void
}

match start(["port": "8080"]) {
  Ok(_) => print "started"
  Fail(error) => print "failed to start: {error}"
}
```

`readPort` liefert einen `ConfigError`, aber `start` liefert einen `AppError` – `?` wandelt den einen über ein
erzeugtes `From` in den anderen um, weil `AppError.Config` der eine Fall ist, der einen `ConfigError` umschließt, und
kein anderer. Die genaue Regel dafür steht in
[The question mark operator](../language/errors/question-mark.md).

## Abwesenheit: Option

Abwesenheit wird auf dieselbe Art modelliert, mit `Option<Value>`, geschrieben als `Value?`:

```trb
type User {
  id: Int
  name: String
}

const users = [User(1, "Ada"), User(2, "Grace")]

fn findUser(id: Int): User? {
  users.find { _.id == id }
}

const name = findUser(2)?.name ?? "nobody"
print name
```

`?.` bildet über die `Option` ab, statt sie zu entpacken, und `??` gibt den Ersatzwert, wenn sie `None` ist. Siehe
[Optional chaining](../language/errors/option-chaining.md).

## Panic ist für Bugs, nicht für erwartete Fehlschläge

```trb
fn percentageOf(part: Int, total: Int): Int {
  if total == 0 {
    panic "total must not be zero"
  }
  part * 100 / total
}

print percentageOf(1, 4)
```

Ein `panic` gibt `panic: <message>` und die Aufrufstelle auf der Standardfehlerausgabe aus, endet mit Exitcode 101,
und nichts läuft danach noch weiter. Es lässt sich nicht abfangen, weil es sagt, dass das Programm einen Zustand
erreicht hat, den sein Autor für unmöglich hielt – ein erwarteter Fehlschlag ist ein `Fail`, kein `panic`. Siehe
[panic](../language/errors/panic.md).

## Weiter

- [Collections and pipelines](collections-and-pipelines.md) - eine Kollektion bauen, lesen und transformieren.
- [Result](../language/errors/result.md) - das genaue Vokabular zu `Ok` und `Fail`.
- [Declaring an error type](../language/errors/error-types.md) - ein Typ mit Fällen, und wann `From` dafür erzeugt
  wird.
