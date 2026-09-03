# 480p UI font atlas

v0.5.2 replaces the original hand-coded 8×10 bitmap font with a pre-rasterized anti-aliased proportional ASCII atlas (`data/ui_font.bin`) and generated advance metrics (`include/ui_font_metrics.h`).

The glyph design used to rasterize this atlas is Inter, distributed under the SIL Open Font License. The project deliberately does **not** include or redistribute a `.ttf`, `.otf`, `.woff`, or other font file; only the rendered GameCube RGB5A3 texture pixels and numeric glyph advances are present.

The atlas is 512×256, uses 32×32 glyph cells, and is rendered with GX linear filtering and half-texel UV insets for clean 640×480 output.

Inter project: https://github.com/rsms/inter
Inter website: https://rsms.me/inter/
License information: https://openfontlicense.org/
