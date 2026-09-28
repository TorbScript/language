---
title: One of several cases
summary: A type can list the cases a value can be, like sunny or rainy, and match does one thing per case and makes sure no case is forgotten.
kind: lesson
status: stable
order: 9
---

Some values are one of a few kinds: the weather is sunny, or it rains. A type can list those kinds with `case`. A case
can carry values of its own: rain comes with how much of it fell.

`match` looks at a value and runs the line of the case it is. Inside `match`, a case is written with a dot in front,
`.Sunny`, and the values it carries get names, like `millimetres`.

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

## No case forgotten

A `match` has to handle every case. When you add a case to the type and forget it in a `match`, the program does not
run, and the computer names the case that is missing. So a new case cannot slip through unnoticed.

<details>
<summary>What does that look like?</summary>

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

## Exercise

Add a case `Windy` to the type and a line for it to the `match`, so the program prints `Hold on to your hat.`

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
<summary>Hint</summary>

Write `case Windy` below the other cases, add `.Windy => "Hold on to your hat."` to the `match`, and print
`advice(Weather.Windy)`.

</details>

<details>
<summary>Solution</summary>

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

## Recap

`case` lists what a value can be, and `match` runs one line per case and refuses to forget one.
