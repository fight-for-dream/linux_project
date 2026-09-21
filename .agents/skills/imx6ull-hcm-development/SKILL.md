---
name: imx6ull-hcm-development
description: Develop, debug, review, and extend this repository's 100ASK i.MX6ULL HCM Linux drivers, device-tree fragments, C/pthreads/LVGL/MQTT applications, and startup scripts. Use when work depends on this project's hardware mapping, Linux 4.9.88 compatibility, numbered stages, or Windows-PC/Ubuntu/board workflow; do not use for unrelated repositories or generic Git/Codex questions.
---

# i.MX6ULL HCM Development

Use this workflow to produce changes that fit the project's hardware, teaching style,
stage history, and three-machine development process.

## Load the Project State

1. Read `../../../PROJECT_CONTEXT.md` from this skill directory and the nearest
   applicable `AGENTS.md`.
2. Read the repository `README.md`, then inspect the target stage and its callers with
   `rg` before editing.
3. Treat source code and the user's latest command output as stronger evidence than the
   context document. Update `PROJECT_CONTEXT.md` when a stable fact changes.
4. Separate facts into source-confirmed, board-verified, and pending confirmation.

## Choose the Baseline

- Use the stage named by the user.
- For a new feature, preserve tested snapshots and start from the newest relevant verified
  stage unless the user explicitly asks to edit an existing stage.
- Use the fixed standalone driver stages listed in `PROJECT_CONTEXT.md` when reviewing or
  extending a device driver.
- Distinguish the newest source stage from the application currently deployed by the
  startup script. Do not silently change the deployment target.
- When an authorized task changes the startup stage, update `HCM_HOME` and `APP_PATH`
  together, then verify that the target executable exists and is executable.

## Inspect Before Implementing

- Search the relevant structures, ABI definitions, device nodes, compatible strings,
  property names, MQTT Topics, and all call sites.
- For hardware changes, inspect the schematic, module data sheet, DTS/DTSI, pinctrl, and
  the complete GPIO/I2C/SPI allocation. Pay special attention to ADXL345 INT1 and motor
  GPIO overlap.
- For kernel code, verify APIs and signatures against the Linux 4.9.88 source tree.
- For timing or concurrency problems, identify the protected state, execution context,
  wake-up condition, and blocking behavior before changing synchronization.

## Implement for This Repository

- Keep the existing concise Wei Dongshan teaching style and brace layout.
- Continue using the project's traditional character-device structure. Do not introduce
  `miscdevice` unless the user requests it.
- Keep kernel/user ABI structures identical and preserve standard errno behavior.
- Add synchronization, cleanup, and abstractions only when they solve a concrete
  correctness problem; explain non-obvious mechanisms briefly.
- Treat DHT11 sampling as timing-sensitive. Preserve its minimum interval and validate any
  change to preemption/interrupt handling and GPIO timing.
- Keep device-tree compatible strings, property names, GPIO polarity, bus address, and
  SPI mode consistent with the driver.
- Preserve simultaneous operation of DHT11, ADXL345, EEPROM, and motor.
- Do not copy complete kernel, LVGL, mqttclient, toolchain, or build products into the repo.

## Respect the Environment Boundary

- Windows PC: edit, inspect, document, and manage Git.
- Ubuntu VM: cross-build modules, applications, LVGL, mqttclient, and DTB.
- i.MX6ULL board: load modules, run applications, inspect logs, and validate hardware.
- Label every command with its execution environment.
- If Ubuntu or the board is unavailable, finish the repository-side work and provide exact
  commands, while marking compilation and hardware checks as pending.
- Do not change board network configuration unless the user explicitly includes it in the
  task.

## Validate

Read [references/validation-matrix.md](references/validation-matrix.md) when the task
changes code, DTS, deployment, or runtime behavior. Select only the relevant sections.
Never claim a build or board test passed without actual output.

## Report the Result

Include:

- files changed and the selected baseline;
- behavior and reasoning in plain Chinese;
- impact on hardware resources, ABI, threads, and existing stages;
- checks actually run and their results;
- copyable Ubuntu build, ADB deployment, and board test commands as applicable;
- remaining unverified items;
- any stable environment fact added to `PROJECT_CONTEXT.md`.
