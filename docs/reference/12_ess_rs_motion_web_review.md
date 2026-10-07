# ESS23-RS20 high-subdivision motion: manual and web review

Reviewed **7 October 2026**. Scope: the user-identified ESS23-RS20 with readable
model `0x4EEA`, firmware word `0x0029` and undocumented algorithm value `3`.
This is a source review, not another hardware run. The
[measured matrix and raw evidence](../reports/2026-10-07_position_completion.md)
remain the authority for what this particular motor actually reported.

## Findings

**The cause of persistent RUNNING is still unresolved.** The exact hardware
manual contains a potentially important **200 kHz pulse-frequency maximum**.
Its applicability to internally generated RS485 positioning is unspecified.
This deserves an explicit vendor answer; it must neither be ignored nor
silently turned into a universal software speed limit.

No matching public ESS23-RS20 failure report, firmware correction, version-word
mapping or supported motor firmware updater was found in the searches below.
That describes the search result, not proof that none exists. Forum reports and
general FAQs concern mostly different drives and do not establish a fix here.

## Exact-model manuals: the frequency question

The preserved [ESS23-RS hardware manual](../vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf#page=5),
physical page 5 / printed page 3, section 2.1, explicitly lists “Pulse frequency”
with a maximum of **200 kHz**. This was visually rechecked in the original PDF.
It is present in the **RS** manual, not merely in the older pulse-input ESS23-20
manual. However, the table does not identify an internal pulse generator or
give a baud/subdivision/speed relationship. Page 9 separately specifies input
levels lasting over 10 ms for X0–X3. Those slow external control inputs do not
explain the 200 kHz row. The row's intended interface remains an ambiguity.

For comparison only, the command-equivalent rate at constant speed is:

`increments/second = command subdivisions/revolution × rpm / 60`

This is arithmetic, **not an RS485 packet rate or a measured internal clock**.
If 200,000 increments/s were an internal limit, it would imply:

| Subdivisions/revolution | RPM at 200,000 increments/s | Equivalent rate at requested 2,000 rpm |
| ---: | ---: | ---: |
| 1,600 | 7,500 (above the documented speed-field range) | 53,333.3/s |
| 12,800 | 937.5 | 426,666.7/s |
| 25,600 | 468.75 | 853,333.3/s |
| 51,200 | 234.375 | 1,706,666.7/s |

These conditional numbers are **not qualified operating limits**. In particular,
the 51,200/2,000-rpm request is approximately 8.53 times the listed frequency,
but its actual peak speed is unmeasured.

The [function manual](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf#page=15),
physical page 15 / printed page 13, explicitly shows that short position moves
can decelerate before reaching requested speed. Pages 69–70 independently allow
subdivision 400–51,200, positioning speed 0–3,000 rpm and ramp words 0–2,000
labelled ms. Separate legal field ranges do not qualify every combination.
The short-move diagram also means that a passing high-RPM request does not
prove sustained operation at that RPM. Pages 68–69 define separate arrival,
running and alarm bits, and closed-loop position as subdivision-equivalent
encoder feedback. The speed word is labelled current speed without enough
detail to validate its derivation on this firmware.

The matrix does not vary monotonically with **requested** speed: at 51,200,
100/100 ramps and 12,800 increments, 900 rpm failed three repeats while 2,000 rpm
completed three. At 2,000 rpm the longer 128,000-increment move failed with
100/100 ramps but completed with 2,000/2,000 ramps. This does not establish a
simple requested-speed cutoff. It also does **not rule out an internal rate
limit**, because actual trajectory peaks and firmware ramp arithmetic are not
known. The previous report's stronger dismissal has been corrected.

## Vendor FAQs: useful context and applicability limits

The following are vendor-authored sources accessed on 7 October. Dates below
are the pages' displayed update dates, not firmware release dates.

| Source | Relevant information | Application to this investigation |
| --- | --- | --- |
| [Microstepping selection and limitations](https://help.omc-stepperonline.com/hc/s/articles/segmentation-choices-and-usage-limitations-of-t-series-closed-loop-drivers), 14 July 2026 | Recommends 1,600–6,400 pulses/revolution for ordinary use; discusses lower subdivision for high speed and higher subdivision for low-speed smoothness. | T-series guidance, not an ESS-RS rating. Consistent with trying lower subdivision, but does not explain RUNNING or qualify a new default. |
| [Rotational speed feedback](https://help.omc-stepperonline.com/hc/s/articles/can-stepperonlines-closed-loop-drives-feedback-rotational-speed), 14 July 2026 | Says fieldbus drives can return motion feedback; distinguishes pulse/direction products. | General capability statement. It does not establish whether ESS firmware `0x0029` reports measured, commanded or filtered speed in the failing state. |
| [Position-error alarm](https://help.omc-stepperonline.com/hc/s/articles/position-error-alarm-in-t-series-drives-triggering-and-sensitivity-adjustment), 14 July 2026 | Following-error alarm depends on exceeding a configured threshold. | T-series thresholds and alarm numbering must not be copied. No alarm alone cannot prove exact positioning or completion. |
| [Lead-angle versus vector algorithms](https://help.omc-stepperonline.com/hc/s/articles/difference-between-lead-angle-algorithm-and-vector-mode-algorithm-in-y-series-closed-loop-driver-operating-mode), 14 July 2026 | Discusses CL86Y operating-mode tradeoffs and legacy CL57Y modes. | Does not decode ESS algorithm `3`. No algorithm switch, encoder change or gain tuning follows from this page. |
| [ICL-RS vibration](https://help.omc-stepperonline.com/hc/s/articles/how-to-resolve-axis-vibration-of-icl-rs-all-in-one-machine-via-software-settings), 14 July 2026 | Discusses mechanical checks, loop gains and mode-switching parameters. | Different integrated family and symptom. Its parameter numbers are not ESS aliases; no demonstrated connection to our persistent RUNNING. |
| [Modbus troubleshooting](https://help.omc-stepperonline.com/hc/s/articles/troubleshooting-guide-modbus-485-communication-issues-with-stepperbrushless-motors), 14 July 2026 | Check wiring, serial tuple, address and frame contents. | Relevant diagnostic categories. Its CL57RS/ICL-RS example writes use a different register map and must not be sent to ESS. Our failing cases already have checked setup, trigger and continuing state replies. |
| [CL86T-V41 PEND configuration](https://help.omc-stepperonline.com/hc/s/articles/1787880803-how-to-correctly-set-up-and-trigger-the-pend-signal-on-the-cl86_t_v41), 28 August 2026 | Discusses an external position-reached output and its configuration. | Does not redefine ESS serial status bits or require a PEND wire on this serial-only bench. |

## Forum and community search

Exact-name searches included `"ESS23-RS20" firmware update`,
`"ESS23-RS" speed running`, `"ESS23-RS20" "0x0029"`, and domain-restricted
searches of Arduino Forum, Reddit, LinuxCNC and GitHub. Broader searches included
ESS23-RS10, ESS17-RS, CL57RS and StepperOnline closed-loop microstepping problems.
Indexed results yielded no independently reproducible matching ESS23-RS20 case.
Search indexing is incomplete; absence in these results is not absence everywhere.

These firsthand threads were reviewed as analogies only:

- [CL86T noisy velocity feedback and poor closed-loop behavior](https://www.reddit.com/r/Motors/comments/1i97f7e/),
  started 24 January 2025: the owner reports velocity noise, vibration and an
  alarm on a different driver/motor. No verified ESS fix or matching raw state
  trace appears in the reviewed thread. It supports asking how speed feedback
  is derived, not diagnosing our motor or importing proposed tuning.
- [CL57T closed-loop question](https://forums.masso.com.au/threads/cl57t-closed-loop-stepper-question.2526/post-18535),
  started 10 April 2022: holding torque but no controller-commanded movement,
  involving pulse input, wiring and controller subdivision configuration.
  Different from an acknowledged RS485 move that travels and remains RUNNING.
- [CL42T-V41 not moving](https://www.reddit.com/r/Motors/comments/1h66jqw/),
  started 4 December 2024: pulse/enable signal-level discussion. Suggestions to
  disconnect inputs or switch open loop are unverified for ESS and are not
  instructions for this bench.

Search noise was substantial: Ethernet SmoothStepper is also called ESS;
ESS23-20 without **RS** is a different control interface. Neither its pulse
timing advice nor CLRS PR-register examples establish ESS-RS semantics. The
200 kHz entry above is retained because it was verified in the exact RS manual.

## Manual updates and motor firmware

The [exact product page](https://www.omc-stepperonline.com/ess-series-2-2nm-311-55oz-in-nema-23-integrated-rs485-closed-loop-stepper-servo-motor-24-48vdc-1000ppr-ess23-rs20)
still lists function manual V1.0, the ESS23-RS10/20 hardware manual and RS20
datasheet. This review found no newer linked revision or erratum resolving the
frequency row. The current direct PDF endpoint could not be fetched during this
review; no fresh byte-equality claim is made. Original snapshots and hashes in
[sources.json](sources.json) remain unchanged.

The [vendor software repository](https://github.com/StepperOnline/Tunning-Software)
lists PC tuning applications, including Y-series V1.2.7 and 7.4. Those numbers
are not motor firmware versions. The
[Y-series software guide](https://help.omc-stepperonline.com/hc/s/articles/how-to-use-the-y-series-closed-loop-driver-software)
does not establish an ESS23-RS20 bootloader, compatible firmware image or upgrade
procedure. No executable was run and no motor firmware was flashed.

`read identity` can read version word `0x0029` from the drive. Its commercial
version/date mapping, whether a newer compatible firmware exists, and whether
field updating is supported remain unknown. Updating the ESP32 cannot update
the motor's internal controller. Do not infer that motor updating is impossible;
request exact hardware compatibility, image checksum, procedure and recovery
instructions from the vendor before attempting it.

## Engineering disposition and next evidence

1. Keep the demonstrated failure visible for raw firmware `0x0029`; do not
   declare every legal speed/subdivision combination supported. Lower-subdivision
   passing cases are recorded alternatives, not a guarantee for every distance.
2. Ask the vendor whether the **RS manual's 200 kHz limit applies to internal
   serial positioning**, and request the actual subdivision/speed/ramp envelope.
   Include the checked identity/configuration, short-versus-long matrix and
   normal-stop failure followed by confirmed fast stop from the existing report.
   Also request the definition of algorithm `3`, speed feedback and firmware
   `0x0029`. These questions are prepared, not sent on the user's behalf.
3. A follow-up rate experiment should bracket 200,000 equivalent increments/s
   at several subdivisions with finite distances and recorded actual staging.
   Separate requested speed, feedback-derived average and independently measured
   peak speed; a short triangular move is not a sustained-rate qualification.
   Plan explicit fast-stop cleanup and restoration, and preserve failures.
   This review did not run that additional experiment.
4. Preserve the existing completion contract: fresh correlated stopped endpoint
   evidence can finish a short move whose RUNNING interval was missed. Persistent
   RUNNING cannot be overridden just because feedback is near the target.
   No automatic replay, hidden retuning, blanket timeout extension or guessed
   frequency clamp was introduced by this review.

This documentation adds no electrical or shaft measurements and does not close
the remaining motor-side issue. The source pages establish questions to resolve,
not a definitive firmware-bug diagnosis.

## Repository verification

Documentation-only change; no runtime image or motor settings changed and no
new HIL result is claimed. `python scripts/check_repository.py` passed reference,
documentation-link and metadata checks. The existing Release build's
`ctest --test-dir build/position_followup_final/native -C Release --output-on-failure`
passed all 77 registered tests. Exact-commit CI is checked after pushing through
the repository's normal GCC, Clang, Arduino and native-IDF jobs.
