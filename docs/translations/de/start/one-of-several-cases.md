---
title: Einer von mehreren Fällen
summary: Ein Typ kann die Fälle auflisten, die ein Wert sein kann, etwa sonnig oder regnerisch, und match tut eines pro Fall und sorgt dafür, dass keiner vergessen wird.
kind: lesson
status: stable
order: 9
translates: ab253b7ecda2
---

Manche Werte sind einer von wenigen Arten: Das Wetter ist sonnig, oder es regnet. Ein Typ kann diese Arten mit
`case` auflisten. Ein Fall kann eigene Werte mitbringen: Regen kommt mit der Menge, die gefallen ist.

`match` schaut sich einen Wert an und führt die Zeile des Falls aus, der er ist. In `match` wird ein Fall mit einem
Punkt davor geschrieben, `.Sunny`, und die Werte, die er mitbringt, bekommen Namen, wie `millimetres`.

```trb run
type Weather {
  case Sunny
  case Rainy(millimetres: Int)
}

fn advice(weather: Weather): String {
  match weather {
    .Sunny => "Wear a hat."
    .Rainy(millimetres) => "Take an umbrella: {millimetres} mm of rain."
  }
}

print advice(Weather.Sunny)
print advice(Weather.Rainy(5))
// prints Wear a hat.
// prints Take an umbrella: 5 mm of rain.
```

## Kein Fall vergessen

Ein `match` muss jeden Fall behandeln. Fügst du dem Typ einen Fall hinzu und vergisst ihn in einem `match`, läuft
das Programm nicht, und der Computer nennt den Fall, der fehlt. So kann sich kein neuer Fall unbemerkt einschleichen.

<details>
<summary>Wie sieht das aus?</summary>

```trb error
type Weather {
  case Sunny
  case Snowy
}

fn advice(weather: Weather): String {
  match weather {
    .Sunny => "Wear a hat."
  }
}

print advice(Weather.Snowy)
// error: `match` does not handle `.Snowy`
```

</details>

## Übung

Füge dem Typ einen Fall `Windy` hinzu und dem `match` eine Zeile dafür, sodass das Programm `Hold on to your hat.`
ausgibt.

```trb exercise
type Weather {
  case Sunny
  case Rainy(millimetres: Int)
}

fn advice(weather: Weather): String {
  match weather {
    .Sunny => "Wear a hat."
    .Rainy(millimetres) => "Take an umbrella: {millimetres} mm of rain."
  }
}

print advice(Weather.Sunny)
```

<details>
<summary>Tipp</summary>

Schreib `case Windy` unter die anderen Fälle, füge `.Windy => "Hold on to your hat."` zum `match` hinzu und gib
`advice(Weather.Windy)` aus.

</details>

<details>
<summary>Lösung</summary>

```trb run
type Weather {
  case Sunny
  case Rainy(millimetres: Int)
  case Windy
}

fn advice(weather: Weather): String {
  match weather {
    .Sunny => "Wear a hat."
    .Rainy(millimetres) => "Take an umbrella: {millimetres} mm of rain."
    .Windy => "Hold on to your hat."
  }
}

print advice(Weather.Windy)
// prints Hold on to your hat.
```

</details>

## Zusammenfassung

`case` listet auf, was ein Wert sein kann, und `match` führt eine Zeile pro Fall aus und weigert sich, einen zu
vergessen.
