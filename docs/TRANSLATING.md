# Translating the game text (PS Vita)

The Vita port can show the game's text in another language: in-game messages,
pickups, weapon and item names, hints, the training level, multiplayer messages and
the subtitles of the dialogue. The
voices and the text drawn inside pictures (like the briefing papers) stay as they
are.

Players pick it in the Vita settings (tap Select twice) > **Display** >
**Text Language**. "Game" keeps the language of their own game files.

| Language | File | Status |
|---|---|---|
| Português (Brasil) | [`misc/vita/lang/pt.txt`](../misc/vita/lang/pt.txt) | done |

## Adding a language

1. Copy `misc/vita/lang/pt.txt` to a new file named with the language code, for
   example `fr.txt` for French or `es.txt` for Spanish.
2. In every line, translate **only the second string**:
   ```
   { "Game Auto Saved" "Partie sauvegardée automatiquement" }
   ```
   The first string is the original English text the game looks for. Changing it
   breaks the line.
3. Keep the spaces at the start and end of the second string, and the `( ` and
   ` )` pieces. Many lines are parts of one sentence with a key name in between:
   ```
   { "Press your use key ( " "Appuyez sur la touche utiliser ( " }
   { " ) to open these doors." " ) pour ouvrir ces portes." }
   ```
4. `&&&` stands for a part the game fills in, like a number or a weapon name. Keep it
   in the translation, where it fits your language:
   ```
   { "Got &&& Rifle Rounds" "Munitions de fusil : &&&" }
   ```
5. Rules for the text:
   - UTF-8 file. Accented Latin letters are fine (é à ç ñ ü ...). Other scripts
     (Cyrillic, Greek, Chinese, Japanese, emoji) cannot be shown by the game's fonts.
   - No double quotes (`"`) inside a string.
   - Lines starting with `//` are comments.
6. Send it:
   - with a pull request that adds the file, or
   - by opening an issue with the **Translation** form and attaching the file.

The maintainer adds the language to the menu and builds a `.vpk` for you to test.

## Asking for a language

Open an issue with the **Translation** form and say which language you would like.
A native speaker who can review the text helps a lot.
