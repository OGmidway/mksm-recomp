# Controllers

Double-click ../run.cmd after connecting an Xbox, DualSense or DualShock 4 controller. The runtime uses raylib/GLFW's bundled device mappings and translates them to PS2 controls. Player 1 uses the first available controller; player 2 uses the second. Keyboard input also remains available for player 1.

| PS2 control | PlayStation | Xbox |
| --- | --- | --- |
| Cross / confirm | X (Cross) | A |
| Circle | Circle | B |
| Square | Square | X |
| Triangle | Triangle | Y |
| Start | Options | Menu |
| Select | Share/Create | View |
| L1/R1 | L1/R1 | LB/RB |
| L2/R2 | L2/R2 | LT/RT |

D-pad, sticks and stick clicks retain their usual positions. Use a normal press and release to confirm menus. This implementation does not add rumble, adaptive triggers, lightbar control or pressure-sensitive face buttons.

## Configuration

Launchers load config/controllers.ini through PS2_CONTROLLER_CONFIG. Restart the game after editing it. Device is a zero-based index among connected controllers; -1 disables that profile. Unspecified ports use their defaults. The file supports symbolic buttons, signed stick axes, deadzone, axis scale and trigger threshold.

The current profile imports Pad1 from the user's PCSX2.ini, including axis scale 1.33 and zero deadzone. Pad2 used keyboard bindings in PCSX2 and retains the recomp's default second-controller mapping. To import another profile from the game folder:

```powershell
python tools/import-pcsx2-controller.py "<user-home>/Documents/PCSX2/inis/PCSX2.ini"
```

The importer supports standard SDL and XInput bindings and reports unsupported bindings rather than guessing raw joystick numbers. It does not modify PCSX2's source configuration. Conversion results are recorded in logs/controller-import.json.

## Verification

Portable native tests cover button encoding, remapping, release/disconnect, two independent ports, triggers, deadzones, axis scaling and invalid configuration. Python checks the bundled Windows Xbox One, DualShock 4 and DualSense device mappings.

A live DualSense run recorded physical Cross presses and releases and progressed through warning screens to the title screen. Player 2 stayed neutral. Physical D-pad, Options, analog sticks, Xbox hardware and DualShock hardware still need live confirmation. See logs/controller-live-probe.json and logs/controller-profile-probe.json. These checks do not establish playable gameplay.
