# EvoFaderWing - [Wiki](https://github.com/stagehandshawn/EvoFaderWing/wiki)

<table width="100%">
  <tr>
    <td width="70%">
      <table width="100%">
        <tr>
          <td align="left">
            <strong>Author:</strong> ShawnR
          </td>
          <td align="right">
            <strong>Contact:</strong> <a href="mailto:stagehandshawn@gmail.com">stagehandshawn@gmail.com</a>
          </td>
        </tr>
      </table>
      <br>
      OSC control of grandMA3 faders with motorized feedback, backlit faders (follows appearances), 20 encoders, and 40 standard exec buttons.
      <br><br><blockquote>
        <strong>Note:</strong> This Guide is a work in progress but EvoFaderWing is fully functional. I continue to work on the repo as time permits.
      </blockquote>
    </td>
    <td width="30%" align="right" valign="top">
      <img src="https://github.com/stagehandshawn/EvoFaderWing/blob/main/docs/gallery/faderwing_left_front.jpg" alt="faderwing" width="350">
    </td>
  </tr>
</table>

[Bill Of Materials](https://github.com/stagehandshawn/EvoFaderWing/wiki/Bill-of-materials)

[Wiring Diagrams](https://github.com/stagehandshawn/EvoFaderWing/wiki/Wiring-Diagrams)

Any questions can be sent to me at [stagehandshawn@gmail.com](mailto:stagehandshawn@gmail.com). I'll help when I can.

---
## Project Images

<table>
  <tr>
    <td width="50%">
      <img src="https://github.com/stagehandshawn/EvoFaderWing/blob/main/docs/gallery/faderwing_top1.jpg" alt="faderwing top" width="100%">
    </td>
    <td width="50%">
      <img src="https://github.com/stagehandshawn/EvoFaderWing/blob/main/docs/gallery/faderwing_right.jpg" alt="faderwing right side" width="100%">
    </td>
  </tr>
  <tr>
    <td width="50%">
      <img src="https://github.com/stagehandshawn/EvoFaderWing/blob/main/docs/gallery/faderwing_back.jpg" alt="faderwing back" width="100%">
    </td>
    <td width="50%">
      <img src="https://github.com/stagehandshawn/EvoFaderWing/blob/main/docs/gallery/faderwing_elec_bay.jpg" alt="electronics bay" width="100%">
    </td>
  </tr>
</table>

**Watch the demo video**

[![Watch the demo video](https://img.youtube.com/vi/cR3AajQKoAo/0.jpg)](https://www.youtube.com/watch?v=cR3AajQKoAo)
[![Watch the demo video](https://img.youtube.com/vi/fbl81pGS5f4/0.jpg)](https://www.youtube.com/watch?v=fbl81pGS5f4)

---

---

## Required repositories

There are 3 repositories you will need to complete this build. This one is the main controller. The other two are:

- **EvoFaderWing_keyboard_i2c**
- **EvoFaderWing_encoder_i2c**

These run on ATmega328p MCUs and are I2C slaves for getting encoder data from 20 encoders (5 each on 4 ATmega328p's), and 1 ATmega for the keyboard matrix.

There is a Python script and a `tasks.json` for uploading code automatically for when the FaderWing is closed and you cannot get to the bootloader button.

## Required Lua

The Lua script `/lua/EvoFaderWingOSC.lua` will poll executors and send updates to the FaderWing using bundled OSC messages, and the FaderWing will send OSC back to the script.

- You will need to create 2 OSC connections
  - The first will be for incoming messages and will be set to recieve.
  - The second will be from outgoing messages and will be set to send.
  - Both will need to be set to a fader range of 100

![OscSettings](https://github.com/stagehandshawn/EvoFaderWing/blob/main/docs/OscSettings.png)

More to come. Thank you for checking out my project!
