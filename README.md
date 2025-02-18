# hid-proxy

**Do NOT use in production**

This is a proof of concept had has may bodges, including:

1. No verification that we do not overflow buffers, such as the keystroke store etc.
2. Fairly crappy encryption - the raw keystrokes fed into SHA256 to form an encryption key. An IV is also used. 
3. Absolutely rubbish user interface. No feedback of current state etc. 
4. The code is also rubbish. If I wasn't throwing it away I'd rewrite it.


## Using the Absolutely Rubbish User Interface

Normally, keystrokes are passed through from the keyboard to the host computer. To make
something special happen you have to do a "double shift":

* Simultaneously press *both* shift keys.
* Release all keys.

The next keystroke defines what action to take:

| Key   | Description                                                                                                                                                                    |
|-------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ENTER | Start entering the passphrase to unlock the key definitions. Press ENTER at the end of the passphrase.                                                                         |
| ESC   | Cancel current operation.                                                                                                                                                      |
| DEL   | Delete everything (erase encryption key and keydefs in flash)                                                                                                                  |
| END   | Lock  keydefs. Will need to re-enter passphrase to unlock.                                                                                                                     |
| =     | Start defining a new / replacement key definition. The next keystroke is the key you are defining; following keys are the replacement text. Use a "double shift" to terminate. |
| PRINT | Write key to NFC tag                                                                                                                                                           |

## Flashing

Usually easiest from CLion, running "ocd" debug config.

Or, hold down both shift keys and the PAUSE key at the same time.

## Serial.

minicom -D /dev/ttyACM0 -b 115200

## Mac Keyboard

rm /Library/Preferences/com.apple.keyboardtype.plist
reboot
