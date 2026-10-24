# The `.ir` file format

STARK ONE stores IR captures as small text files under `/sd/stark/captures/`
(`ir_NNNN.ir`, TASKS.md STARK-0305) so they can be read and edited on a PC. Parsed and
written by `components/stark_ir/ir_raw.c` (STARK-0302).

```
stark-ir 1
protocol NEC
address 0x0010
command 0x0012
carrier 38000
timings 9000 4500 560 560 560 1690 … 560
```

| Line | Meaning |
| --- | --- |
| `stark-ir 1` | Format and version; must be the first line that is not blank or a comment |
| `timings …` | **Required.** 1–256 durations in microseconds, alternating mark and space, starting with a mark; each 1–65535. Decimal or `0x` hex |
| `carrier N` | Carrier frequency in Hz, 20000–60000. Missing: the decoded protocol's (NEC/NECX 38000, RC5/RC6 36000, SIRC 40000), 38000 for raw |
| `protocol`, `address`, `command`, `toggle 1`, `repeat 1` | Informational, written by the firmware. On reading they are **re-derived from the timings**; a file whose fields disagree with its timings takes the decoded values |

Anything after `#` on a line is a comment; unknown keys are ignored; a line longer than
1700 characters, a missing header or `timings`, or an out-of-range value makes the whole
file invalid (the app shows it as `(invalid)` and will not send it).

**Replay sends the timings**, with the carrier, through the transmit gate (STARK-0304:
at most 1 s of signal, at most 3 repeats, at least 1 s between transmissions, after a
confirmation). The format holds signals of our own devices (SECURITY_SCOPE §2); it is not
a code database.
