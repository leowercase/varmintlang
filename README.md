<img src="./web/varproto.svg" width="200px" alt="Varmint logo">

*simple* - *fun* - *abrasive* - *absolute pain in the ass*

# Varmint
Varmint is a silly little language I created after getting interested on how we humans talk to machines.

You can [try it out online](https://leowercase.github.io/varmintlang)!
There is also a [Nix expression](./varmint.nix) that can be `callPackage`d. Example programs can be found at [`examples/`](./examples/) and [`test/`](./test/).

The language can run simple programs but anything too complex gets annoying pretty fast.
*As the name implies, it probably has bugs*

#### Resources used
- [Crafting Interpreters](https://craftinginterpreters.com/) is just amazing, many thanks to the author. I "borrowed" a ton of ideas, concepts and patterns from the book.
- [Simple but powerful Pratt parsing](https://matklad.github.io/2020/04/13/simple-but-powerful-pratt-parsing.html) was very useful in understanding how Pratt parsers work.
- [This Stack Overflow answer](https://stackoverflow.com/questions/220658/the-difference-between-a-closure-and-a-lambda/36878651#36878651) cleared a lot of confusion on closures.

Varmint was my final project for [Harvard CS50x](https://cs50.harvard.edu/x/).

## Structure
**A block** is your friendly neighborhood imperative series of statements.
```
putln "Hello, world!"
putln "How ya doing?"
1 + 2
```
It evaluates to its last statement; the above example would return `3`.

The top level is a block, you can also create a *block expression* inside curly braces `{}`.

## Variables
**Name binding** is accomplished with `var` and `as`. Variables are lexically scoped.
```
var x := "poopenfarten"
{
  var x := "all nice and tidy"
  putln x
}

x as x'
putln x'
```
The `:=` operator denotes assignment.

> [!TIP]
> #### Syntactic sugar
> You can specify multiple declarations in a single `var`:
> ```
> var foo, bar := baz, quux
> ```
> Both `var` and `as` statements have [expression forms](https://en.wikipedia.org/wiki/Let_expression):
> ```
> var a := 3,
>     b := 4
>   in sqrt(a^2 + b^2)
> ```

Any infix operator can be used in a **compound assignment**. These two lines are equivalent:
```
a := a + 5
a +:= 5
```

## Comments
Line comments begin with `#`. Block comments start with `#[` and end with `]#`, and are nestable.
```
# TODO: Fix offensive output: "poopenfarten"
# putln x'
```

## Functions

### Declaring
Specify a list of arguments after a `var` declaration to create a function.
```
var Q(x) := a*x^2 + b*x + c
```
Functions are first-class values. An anonymous function can be created with the **maplet arrow** `=>`.
```
((x, y) => x + y)(1, 2) # Returns 3
```
Signal early returns with the `return` keyword:
```
if spinach_is_good
    then return 3
```

Variables outside the scope of the function can be *closed over*.
Creating the closure means the function now has its own private version of the variable's data,
and the function can refer to it even after the variable itself has gone out of scope.
```
# Inner function closes over upvalue
var create_closure() :=
    var upvalue := 1 in () => upvalue *:= 2

var f := create_closure()

f() # 2
f() # 4
f() # 8
```

Any neighbor declarations in the enclosing `var` can be accessed in the body of a function.
This makes mutual recursion nice to implement, here for example the [Hofstadter Female and Male sequences](https://en.wikipedia.org/wiki/Hofstadter_sequence#Hofstadter_Female_and_Male_sequences):
```
var F(n) := if n = 0 then 1
            else n - M F(n - 1),

    M(n) := if n = 0 then 0
            else n - F M(n - 1)
```
Spiffy.

### Calling

#### Regular style call
```
greet(cat, dog) # "Meowr pfft!!"
```

#### Method-style call
```
dog:greet cat # "Woof woof!"
```
This is also called [uniform function call syntax](https://en.wikipedia.org/wiki/Uniform_function_call_syntax), because it allows you to use functions like methods in OOP.

#### Partial application
```
var welcome_message := server:greet
...
user:welcome_message() # "403 Forbidden - I'm sorry, Dave."
```
When a parameter list is missing, the `:` operator creates a partial application.

## Values
Equality `=` and inequality `/=` can be used to compare values.

#### Booleans
You can use the logical connectives `not`, `and`, `or` and implication `->`.
```
(False -> False) = True
```
Logical operators don't [short-circuit](https://www.cs.utexas.edu/~EWD/ewd10xx/EWD1009.PDF)!

#### Numbers
Basic arithmetic `+`, `-`, `*`, `/` is defined, as well as exponentiation `^` and modulo `mod`.
```
-12.345 / 6
```
Comparison operators can be chained.
```
a < b <= c = d
```

#### Strings
The [escape sequences](https://en.wikipedia.org/wiki/Escape_sequences_in_C#Escape_sequences) `\a`, `\b`, `\e`, `\f`, `\n`, `\r`, `\t`, `\v`, `\\`, `\"` and `\0` are supported.

Expressions and blocks can be interpolated into strings with `\()` and `\{}` respectively:
```
"Quoth the \(fowl), \"Nevermore.\""
```

Linear collections in general can also be concatenated and repeated with `||` and `|*`:
```
var meow := "me" || "ow",
    meow' := meow |* 4 # "meowmeowmeowmeow"
```

## Collections
**Lists** are comma-separated collections with order. **Tables** are collections of `key := value` pairs.
```
var seasons := ["spring", "summer", "autumn", "winter"]

var person := @[
  name := "Antonio",
  ["date of birth"] := 1678,
  hobbies.music := [],
]
```
You can index into collections with brackets `[]`, but string keys can additionally be accessed with the `.` operator.
```
seasons[3] # "winter"
person.hobbies["music"]:push "violin"

# Also works with strings!
putln person.name[0]
```

`?.` and `?[]` return an optional result.
```
table?.key else putln "No match."
```

> [!TIP]
> Index lists from the top with negative indices: `["Fee", "Fi", "Fo", "Fum"][-1]` gives `"Fum"`.

> [!NOTE]
> A table key needs to be hashable: valid types are boolean, number, string and maybe.

## Optional values, `if` and `else`
The maybe type represents optional values, much like `null` in other languages.
It has two possible states: `Some()`, containing a result value, or `None`, representing an empty result.

This is kind of just copying Rust and Haskell's cool result/error handling without actually having support for [algebraic data types](https://en.wikipedia.org/wiki/Algebraic_data_type) :)

#### Unwrapping

The contained value has to be explicitly unwrapped before you can access it, using the `else` keyword:
```
var opinion_on_spinach := Some("yummy")
putln "I think spinach is \(
        opinion_on_spinach else "neutral")"
```
The right hand side of `else` defines the default expression in case of `None`.

> [!TIP]
> The postfix interrobang operator `?!` is synonymous with `else return None`.
> It can be used to defer result handling to the caller.

#### Conditionals

The `if`-`then` expression is a conditional fork in the road.
It evaluates to either `Some()` or `None`, depending on whether the condition is `True`.
```
if seasons[i] = "autumn" then rain()
```
Often you want to use it in conjunction with `else` to handle all possible cases, and add additional conditions with `elif`:
```
var coin := rand()

if coin > 0.5 then
    "Heads!"
elif coin < 0.5 then
    "Tails!"
else
    "The coin landed on its side."
```

## Loops
Three types of loops are supported: `loop`, `while` and `for`.
```
loop
    putln "looping foreverrr..."

while cond do
    putln "looping while cond holds..."

for i in iterator do
    putln "looping while iterator has items..."
```

#### Predicate loop `while`
The `while` loop evaluates a condition expression before each cycle and terminates, if the condition is `False`.

#### Iterator loop `for`
The `for` loop calls a special iterator function before each cycle.
- If the iterator returns `Some()`, the iteration variable is initialized with the unwrapped value.
- If the return value is `None`, the loop terminates.

The `range` builtin returns an iterator that goes through an open-closed interval by increments:
```
for num in range(a, b, inc) do
    computation(num)
```
The `items` builtin returns an iterator that goes through the elements of a collection. For example:
```
for decade in [1980, 1990, 2000]:items() do
    putln "The greatest hits of the \(decade)s"
```

> [!TIP]
> You can easily create your own iterators using a closure:
> ```
> var countdown(x) :=
>     () =>
>       if x >= 0
>       then {
>         x -:= 1
>         x + 1
>       }
>
> for i in countdown(10) do
>     putln "\(i)..."
>
> putln "Happy New Year!"
> ```

#### Control flow
Manually terminate a loop with `break`, and skip to the next iteration with `continue`.
A loop label starting with `'` can be specified after the keyword:
```
var friends := ["alligator (whom I may never see later)", "blue sky", "darkness, my old friend"]

while friends:len() > 0 do 'outer {
    loop 'inner {
        putln "Goodbye, \(friends:pop())"
        continue 'outer
    }
    putln "Hey! What about me?"
}
```

#### Loop results
A loop evaluates to the value of the last cycle before termination.
You can also specify a result after `break` or `continue`, much like with `return`.

## Glossary

### Significant indentation
```
# This is 6:
1 +
2 +
3

# This is also 6:
1
 + 2
 + 3

# These are 1, 2 and 3 on separate lines:
1
+ 2
+ 3
```

When facing ambiguity, Varmint looks at indentation. For the next line to be a continuation, its indent must be
- *greater than* the indent of the first line in the chain
- *greater than or equal to* the last indent.

**Spaces** and **tabs** are both valid choices for indentation.
If you mix and match though, the interpreter will refuse to evaluate - it's ambiguous and just plain bad style.

### Builtins

| Operators (highest precedence first) | Description                           |
| ------------------------------------ | -----------                           |
| `:`                                  | Method-style call                     |
| `()` `[]` `.` `?!`                   | Function call, element access, unwrap |
| unary `%`                            | Percentage (multiply by `0.01`)       |
| `!`                                  | Factorial                             |
| unary `+`, `-`                       | Sign                                  |
| `^`                                  | Raise to power                        |
| `\|*`                                | Repeat (`n`-catenate)                 |
| `\|\|`                               | Concatenate                           |
| `*` `/` `mod` (`%`)                  | Multiply, divide, take modulo         |
| `+` `-`                              | Add, substract                        |
| `not`                                | Logical complement                    |
| `=` `/=` `<` `>` `<=` `>=`           | Comparison                            |
| `->`                                 | Logical implication                   |
| `and`                                | Logical conjunction                   |
| `or`                                 | Logical disjunction                   |
| `break` `continue` `return`          | Control flow                          |
| `if`                                 | Conditional                           |
| `else` `elif`                        | Alternative value                     |
| `:=`                                 | Assignment                            |
| `var` `as` `loop` `for` `while`      | Declaration, loop                     |

| I/O             | Description                    |
| ---             | -----------                    |
| `put(text)`     | Print to stdout                |
| `putln(text)`   | Print to stdout with a newline |
| `input(prompt)` | Prompt the user for input      |

> [!NOTE]
> I/O is limited in the [web playground](https://leowercase.github.io/varmintlang).
> This makes the [tic tac toe game in `examples/`](./examples/tictactoe.var) not really work as it should - if you want to try it out, use the CLI version instead!

| Simple values       | Description                                                  |
| -------------       | -----------                                                  |
| `typeof(val)`       | The type of `val` as a string                                |
| `to_string(val)`    | String representation of `val`                               |
| `unwrap(maybe)`     | Get the value inside a `Some`, raise runtime error if `None` |
| `to_number(string)` | Parse `string` into a number, return result as maybe         |

| Collections             | Description                                            |
| -----------             | -----------                                            |
| `len(collection)`       | The size of `collection`                               |
| `has(collection, elem)` | Whether `elem` ∈ `collection`                          |
| `push(list, elem)`      | Append `elem` to the end of `list`, return `list`      |
| `pop(list)`             | Remove an element from the top of `list` and return it |

| Math                                          | Description                             |
| ----                                          | -----------                             |
| `e`                                           | Euler's number e                        |
| `pi`                                          | Half turn constant π                    |
| `tau`                                         | Full turn constant τ                    |
| `inf`                                         | Infinity ∞                              |
| `abs(a)`                                      | Absolute value                          |
| `sqrt(n)`, `cbrt(n)`                          | Square root and cube root               |
| `ln(x)`, `lg(x)`                              | Natural and common logarithm            |
| `sin(theta)`, `cos(theta)`, `tan(theta)`      | Trig functions                          |
| `asin(x)`, `acos(x)`, `atan(x)`               | Inverse trig functions                  |
| `ceil(x)`, `floor(x)`, `round(x)`, `trunc(x)` | Ceiling, floor, rounding and truncation |
| `rand()`                                      | Random number between `0` and `1`       |

| String conversions | Description                                              |
| ------------------ | -----------                                              |
| `asciify(n)`       | Turn the ASCII code `n` into its corresponding character |
| `char_ord(char)`   | The ASCII code for single byte string `char`             |
| `rot(shift, text)` | Caesar cipher of `shift` positions on `text`             |

