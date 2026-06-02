# SDF Text — แผนการทำ (Priority: รอง)

## สถานะปัจจุบัน

- ✅ SDF pipeline (Metal shader) — พร้อมใช้งาน
- ✅ SdfFont struct + `get_glyph()` — รองรับ Latin + Thai
- ✅ `TextRenderer::render_sdf()` — รองรับ Thai ครบ (front vowel, combining)
- ❌ **ไม่มี SDF atlas baking** — ต้องใช้ `stbtt_GetGlyphSDF()` แทน `stbtt_BakeFontBitmap`
- ❌ **ไม่มี SdfFont init** — ต้อง bake glyphs ลง R16 texture สำหรับ SDF
- ❌ **TextRenderer ไม่ได้ถูก wire กับ main renderer** — `draw_text()` ใช้ sdf_pipeline ผิด (bind กับ bitmap atlas แทนที่จะเป็น SDF atlas)
- ❌ **draw_text() ใช้ SDF pipeline กับ bitmap atlas** — ทำงานได้คร่าวๆ เพราะ R8 เหมือนกัน แต่ไม่มี true distance field interpolation

## สิ่งที่ต้องทำ

### 1. Bake SDF atlas

- ใช้ `stbtt_GetGlyphSDF()` แทน `stbtt_BakeFontBitmap`
- เก็บ signed distance field ลง R16 texture (precision 16-bit)
- สร้าง `FontAtlas` แบบ SDF หรือเพิ่ม method `bake_sdf()` ใน FontAtlas เดิม
- ขนาด atlas 512×512 น่าจะพอ (SDF ขนาดเท่ากัน หรืออาจต้อง 1024×1024 ถ้า range ใหญ่)

### 2. Init SdfFont

- สร้าง `SdfFont::init()` ที่รับ SDF atlas + glyph info
- สร้าง Texture R16_UNORM สำหรับ SDF atlas
- wire `SdfFont` instance เข้ากับ `TextRenderer`

### 3. Add draw_text_sdf() หรือ Sugar

- เพิ่ม method ใน `Renderer` สำหรับ SDF text โดยเฉพาะ
- หรือปรับ `draw_text()` ให้รับ pipeline parameter
- ใช้ MaterialType::SDF + sdf_pipeline จริงๆ

### 4. Thai glyph coverage

- ต้อง bake 0x0E01–0x0E5B (91 chars) เหมือน bitmap
- เพิ่มอักขระเพิ่มเติม: 0x0E30 (Sara A), 0x0E32 (Sara Am), 0x0E33 (Sara Am), 0x0E45 (Lakkhangyao), 0x0E4C (Thanthakhat), 0x0E4F (Fongman), 0x0E50–0x0E59 (ตัวเลขไทย), 0x0E5A–0x0E5B (Angkhankhu, Khomut)

### 5. ทดสอบ

- เปรียบเทียบ quality ระหว่าง SDF vs bitmap ที่ scale 0.5×, 1×, 2×, 4×
- ทดสอบ Thai + Latin ในข้อความเดียวกัน
- Performance: SDF baking ช้ากว่า bitmap มาก ควร bake ตอน init หรือ prebake เป็น resource

## Timeline

Sprint นี้: focus ที่ bitmap path (เสร็จแล้ว)  
Sprint หน้า (หรือเมื่อว่าง): ทำ SDF atlas + wire up
