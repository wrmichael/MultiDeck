# Start-up screen

The same look as RigDeck: "MultiDeck" large in polished gold, with "Firmware launcher by AC9HP" and the version
under it. The gold is computed on the board from the letter shapes (main/ui.cpp, goldText, from RigDeck): soft
bevel lit from the upper left, highlight, metal bands, brushed texture, dark rim, warm shadow. The tiles use the
same renderer for their two-letter monograms.

Font: Audiowide by Astigmatic, SIL Open Font License 1.1 (OFL.txt). The board carries only space, -, /, 0-9,
A-Z and a-z (main/logo_font.c, about 5 kB), made with:

    pyftsubset Audiowide-Regular.ttf --unicodes="U+0020,U+002D,U+002F,U+0030-0039,U+0041-005A,U+0061-007A" \
        --output-file=audiowide_call.ttf --no-hinting --desubroutinize --drop-tables+=GPOS,GSUB,kern

gold_logo.py renders the same look on a PC (PNG preview): `python3 gold_logo.py Audiowide-Regular.ttf MultiDeck multideck.png 760 220`.
