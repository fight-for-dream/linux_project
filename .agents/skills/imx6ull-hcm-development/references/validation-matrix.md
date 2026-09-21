# HCM Validation Matrix

Load only the sections relevant to the current change.

## All Changes

- Inspect the complete diff and run `git diff --check`.
- Confirm that no prior stage was changed unintentionally.
- Confirm declarations, definitions, structure layouts, and call sites agree.
- Check that no binary, object, module, DTB, library, log, CSV, credential, private key,
  or full third-party source tree entered Git.

## Linux Driver

### Ubuntu

- Build against `/home/book/100ask_imx6ull-sdk/Linux-4.9.88` with the ARM Buildroot
  toolchain.
- Review every warning and error.
- Check module architecture, `modinfo`, and `vermagic` when those tools are available.

### Board

- Stop applications that hold the target device before replacing a module.
- Load the module and inspect the new `dmesg` lines.
- Check the expected `/dev/hcm_*` node and run the matching standalone test.
- Reload all four HCM modules together and repeat a basic read/control test.
- For cleanup changes, test removal only after all users close the device.

## Device Tree

- Match `compatible`, property names, GPIO polarity, I2C address, SPI mode, and pinctrl
  between the DTS fragment and driver.
- Check the full resource map, including pins present on connectors but unused by the
  current module.
- Merge the fragment into the actual board DTS and build the DTB used at boot.
- Update the board's real DTB, reboot, and confirm probe output and device-tree nodes.
- Re-test all four modules for resource conflicts.

## DHT11

- Keep reads at least 2 seconds apart.
- Check checksum failures, timeout paths, and repeated reads.
- Verify that the sampling critical section matches the selected driver baseline.
- Confirm the rest of the system remains responsive during sampling.

## ADXL345

- Confirm device ID `0xE5`, SPI mode 3, and 1 MHz maximum configured by this project.
- Test a stationary sample and a clearly changing orientation or vibration.
- If adding INT1, resolve its `GPIO4_IO20` conflict with the motor before coding.

## EEPROM and Configuration

- Keep standalone test data at `0x80` and integrated system configuration at `0x00`.
- Test writes that cross an 8-byte page boundary and verify the readback.
- Check magic, version, checksum, range validation, and defaults after invalid data.
- Save a configuration, restart the application, and verify persistence.

## Motor

- Use bounded steps and interval values and keep the mechanism safe.
- Test both directions and invalid command ranges.
- Verify that user-space structure layout matches the driver.
- Re-test automatic and manual modes after thread or alarm changes.

## Integrated pthread Application

- Cross-build with warnings enabled where the existing build permits.
- Open all four device nodes successfully.
- Test normal readings, one disconnected sensor, recovery, and three-error fault behavior.
- Exercise alarms, condition-variable wakeups, EEPROM save, and motor requests.
- Send SIGINT and SIGTERM; verify all created threads exit and the process terminates.

## LVGL

- Confirm `/dev/fb0` is 1024x600 at 32 bpp and the Goodix event device is correct.
- Test the main screen, settings screen, navigation, button callbacks, and parameter bounds.
- Verify asynchronous save status and manual/automatic motor controls.
- For stage 20, verify the MQTT connection indicator changes without calling LVGL APIs
  unsafely from a worker thread.

## MQTT

- Obtain the broker address from deployment configuration rather than hard-coding a new
  address in source.
- Verify PC and board connectivity on the current hotspot.
- Test data publishing, motor commands, configuration commands, result responses, and
  reconnect behavior.
- For stage 20, verify retained `online`, graceful `offline`, and LWT `offline`.
- Link the already tested mqttclient static library; do not switch back to compiling an
  arbitrary subset of its `.c` files unless the user requests that change.

## Startup Script

- Keep network management outside `S99hcm` unless explicitly requested.
- Test `start`, `status`, `stop`, and `restart`.
- Confirm all four modules, PID file, application executable, and `/tmp/hcm_app.log`.
- Disconnect the ADB shell and confirm the application remains alive.
- Confirm which numbered application stage the configuration intentionally launches.
