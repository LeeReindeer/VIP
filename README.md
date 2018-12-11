# VIP

> VIP - Vi Poor


- [x] 1. Entering raw mode
- [x] 2. Raw input and output
- [x] 3. A text viewer
- [x] 4. A text editor
- [x] 5. Vi operations
- [ ] 6. Search
- [ ] 7. Syntax highlighting

## Vi operations

> `!` notation means **not** support
>
> `~` notation means any numbers

| type   | operator | number | motion  |
| ------ | -------- | ------ | ------- |
| move   |          |        |         |
|        | !        | ~      | h,j,k,l |
|        | !        | !      | gg      |
|        | !        | ~      | G       |
|        | !        | !      | $       |
|        | !        | !      | 0(zero) |
| edit   |          |        |         |
|        | c,d      | ~      | w,e     |
|        | c,d      | !      | $,0     |
|        | y        | ~      | w,e     |
|        | y        | !      | $,0     |
|        | !        | ~      | dd      |
|        | !        | ~      | yy      |
|        | !        | ~      | p       |
|        | !        | ~      | x,X     |
| others |          |        |         |
|        | !        | ~      | J       |
|        | !        | ~      | o,O     |
|        | !        | ~      | u,U     |
|        | !        | !      | a,A     |
|        | !        | !      | r,R     |