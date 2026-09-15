# USE POLICY & FEATURE SCOPE

This document is **binding on design**, not a disclaimer. It is referenced by
[ADR-0013](../DECISIONS.md) and by every radio-related milestone in
[ROADMAP.md](../ROADMAP.md). Feature requests are evaluated against it before they
become tasks.

---

## 1. Intended use

STARK ONE is built for use on:

* electronic devices we own,
* our own RF / NFC / IR test hardware,
* laboratory and bench environments,
* CTF and educational systems,
* systems we have explicit, documented authorisation to test.

## 2. In scope — legitimate embedded & security engineering

These are designed properly, thoroughly, and to a professional standard:

| Capability | Notes |
| --- | --- |
| GPIO read/write/pulse/PWM, frequency & duty measurement | Bench debugging |
| I²C scanning, register read/write, device identification | Bench debugging |
| SPI transaction probing | Bench debugging |
| UART bridging, monitoring, auto-baud detection | Bench debugging |
| Logic-level timing capture and analysis | Protocol work |
| IR receive, decode, and replay **of our own devices** | Remote/appliance development |
| NFC tag reading, technology identification, NDEF read/write **on tags we own** | Tag development |
| Sub-GHz spectrum observation, RSSI, raw capture, bitstream framing analysis | RF development |
| Replay of **our own** recorded signals to **our own** receivers | Test-rig validation |
| Protocol dissection and documentation of open/published protocols | Research |
| Self-test, diagnostics, measurement, logging, export | Product quality |

Receive, analyse, measure and document: these are the core of the product and are not
limited.

## 3. Out of scope — not designed, not implemented, not stubbed

The following are excluded by policy. They are not deprioritised features; they are
non-features. A pull request implementing one is rejected regardless of quality.

* Defeating or circumventing access control of any kind (doors, gates, vehicles,
  alarms, safes, locks, turnstiles).
* Recovering, cracking, or brute-forcing keys of credentials we do not own —
  including Mifare Classic key-recovery attacks (nested, darkside, hardnested),
  default-key sweeps against third-party tags, and equivalent techniques on any
  other credential technology.
* Cloning, emulating, or spoofing identifiers belonging to another person or
  organisation (card UIDs, tag contents, remote identities, RFID badges).
* Rolling-code attacks, code-grabbing, or "jam and replay" against garage doors,
  vehicles, or any access system.
* Jamming, deauthentication, flooding, or any denial-of-service transmission.
* Brute-force or dictionary transmission of codes toward a receiver we do not own.
* Bundled databases of third-party codes, keys, or credentials.
* Covert operation, detection evasion, or anti-forensic features.
* Any capability whose primary purpose is unauthorised access.

## 4. Guardrails that follow from this policy

These are engineering requirements, tracked as acceptance criteria in the relevant
tasks:

1. **Receive-first.** Every radio app defaults to receive-only. Transmit is a separate,
   deliberate action.
2. **Explicit confirmation for transmit.** A modal confirmation naming the frequency,
   duration and payload precedes any RF transmission.
3. **Rate and duty limiting.** Sub-GHz transmission respects a firmware duty-cycle cap
   and a power cap, consistent with the SRD band rules in
   [HARDWARE.md §7](../HARDWARE.md). The limiter is not user-adjustable past the legal
   envelope.
4. **No unattended transmission loops.** No "repeat forever", no sweep-transmit, no
   scheduled emission.
5. **Transmission is logged.** What was sent, when, at what frequency — visible to the
   user and written to the log.
6. **Pin safety.** Lab GPIO tooling refuses reserved pins and confirms before driving
   an output.
7. **Test vectors come from our own hardware** and are documented as such.

## 5. Legal note

Radio transmission is regulated. In Turkey and the EU the 433.05–434.79 MHz SRD band
carries ERP and duty-cycle limits; other bands may require licensing. Compliance is the
operator's responsibility, and the firmware's limiters exist to make compliance the
default rather than an afterthought. Using this device against systems you do not own
or are not authorised to test is illegal in most jurisdictions and is outside the
purpose of this project.

## 6. Applying this policy

When a proposed feature is ambiguous, the test is: **what is the feature's primary
purpose, and what does it do that receiving and analysing cannot?** A capture-and-
analyse view of an unknown protocol is engineering. A one-button "open it" is not.
If the answer is unclear, the feature waits and the question goes into DECISIONS.md.
