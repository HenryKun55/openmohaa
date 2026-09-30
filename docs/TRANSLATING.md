# Translating the game text (PS Vita)

The Vita port can show the game's text in another language: in-game messages,
pickups, weapon and item names, hints, the training level, multiplayer messages and
the subtitles of the dialogue. The
voices stay in English. The words drawn on the menu pictures (signs, buttons, stamps)
come in a separate language pack, see [Menu pictures](#menu-pictures); the briefing
papers stay as they are.

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

## Menu pictures

Some menus are pictures with words drawn on them: the signs of the main menu room, the
buttons, the stamps of the load/save folder, the difficulty board, the mission
blackboards, PAUSED and LOADING. Their translation is a language pack, one per
language: `lang_pt.pk3`, `lang_fr.pk3` and so on. The pack is made from your own game
files (the pictures are EA's art, so they are not shipped), with
[`misc/vita/make_menu_text.py`](../misc/vita/make_menu_text.py):

```
pip3 install pillow numpy
python3 misc/vita/make_menu_text.py /path/to/MOHAA/main --lang pt
```

`/path/to/MOHAA/main` is the game's `main` folder with the `Pak*.pk3` files. The
script writes `lang_pt.pk3`; copy it to `ux0:data/openmohaa/main/` on the Vita and
choose that **Text Language** in the Vita settings. Without the pack the menus keep
their English pictures. `--preview DIR` also saves each picture next to its original,
to check them.

The script paints the English words out and sets the translation with the letters of
the pictures themselves, so the lettering matches. A letter that none of the pictures
has is drawn with a similar font.

To translate the pictures of a new language, translate the lines under
`// Words drawn in menu pictures` in your language file. A `|` splits the translation
over the two lines of a two-line sign:
```
{ "PERSONAL RECORDS" "FICHA|PESSOAL" }
```
Keep them short: a longer word is squeezed, then made smaller, to fit its sign.

## Asking for a language

Open an issue with the **Translation** form and say which language you would like.
A native speaker who can review the text helps a lot.
