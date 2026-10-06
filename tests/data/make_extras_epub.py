#!/usr/bin/env python3
"""Writes extras.epub, the small EPUB the host tests read for what books hold
besides plain paragraphs: pictures, links, footnotes, lists, tables, rules,
and the book's own styles.

Chapter 1 has a JPEG with a caption, a small PNG, a picture wrapped in SVG,
a progressive JPEG and a missing file (both shown as placeholders with their
alt text). Chapter 2 has a footnote reference (epub:type="noteref") with its
note in an <aside>, a numbered link to an endnote in chapter 3, a link to a
heading there, emphasis, centred and unindented paragraphs from the
stylesheet and a style attribute, a rule, nested lists, a table and a
character the fonts lack. Needs Pillow.
"""
import io
import zipfile

from PIL import Image, ImageDraw


def picture(fmt, size, **options):
    """A landscape: sky gradient, a dark hill, a round sun."""
    w, h = size
    img = Image.new("L", size, 255)
    d = ImageDraw.Draw(img)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=230 - 120 * y // h)
    d.ellipse([w * 6 // 10, h // 8, w * 6 // 10 + h // 4, h // 8 + h // 4], fill=250)
    d.polygon([(0, h), (0, h * 2 // 3), (w // 3, h // 2), (w * 2 // 3, h * 3 // 4), (w, h * 3 // 5), (w, h)], fill=40)
    buf = io.BytesIO()
    img.save(buf, fmt, **options)
    return buf.getvalue()


CONTAINER = """<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""

OPF = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="id">vellum-extras</dc:identifier>
    <dc:title>Pictures and Notes</dc:title>
    <dc:creator>Vellum Tests</dc:creator>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="css" href="css/book.css" media-type="text/css"/>
    <item id="hill" href="images/hill.jpg" media-type="image/jpeg"/>
    <item id="dot" href="images/dot.png" media-type="image/png"/>
    <item id="full" href="images/full.jpg" media-type="image/jpeg"/>
    <item id="prog" href="images/progressive.jpg" media-type="image/jpeg"/>
    <item id="c1" href="text/pictures.xhtml" media-type="application/xhtml+xml"/>
    <item id="c2" href="text/notes.xhtml" media-type="application/xhtml+xml"/>
    <item id="c3" href="text/endnotes.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine>
    <itemref idref="c1"/>
    <itemref idref="c2"/>
    <itemref idref="c3"/>
  </spine>
</package>"""

NAV = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<body>
  <nav epub:type="toc"><ol>
    <li><a href="text/pictures.xhtml">Pictures</a></li>
    <li><a href="text/notes.xhtml">Notes and Lists</a></li>
    <li><a href="text/endnotes.xhtml">Endnotes</a></li>
  </ol></nav>
</body>
</html>"""

CSS = """/* The book's look */
p { text-indent: 1.2em; margin: 0 }
p.center, .caption { text-align: center; text-indent: 0 }
.noindent { text-indent: 0 }
span.smallcaps { font-variant: small-caps }
.bold { font-weight: bold }
ul.plain { list-style-type: none }
@media amzn-kf8 { .kindle { text-align: right } }
div.poem > p { margin-left: 2em }
"""

PICTURES = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>Pictures</title><link rel="stylesheet" type="text/css" href="../css/book.css"/></head>
<body>
  <h1>Pictures</h1>
  <p>The hill at dawn, before anyone was up.</p>
  <div class="figure"><img src="../images/hill.jpg" alt="A hill under a rising sun"/></div>
  <p class="caption">The hill, drawn small.</p>
  <p>A dot <img src="../images/dot.png" alt="dot"/> in the middle of a line.</p>
  <div><svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 900 1400">
    <image width="900" height="1400" xlink:href="../images/full.jpg"/></svg></div>
  <p>After the full-page picture.</p>
  <p><img src="../images/progressive.jpg" alt="A progressive picture"/></p>
  <p><img src="../images/missing.png" alt="Lost map of the island"/></p>
  <p>The end of the pictures.</p>
</body>
</html>"""

NOTES = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Notes</title><link rel="stylesheet" href="../css/book.css"/>
<style>.note-text { font-style: italic }</style></head>
<body>
  <h2>Notes and Lists</h2>
  <p class="noindent">The river ran <i>quickly</i> past the <b>old mill</b>.<a epub:type="noteref" href="#fn1" id="r1">1</a> Nobody knew who built it.<a href="endnotes.xhtml#en2"><sup>2</sup></a></p>
  <p>See <a href="endnotes.xhtml#part">the second part</a> for more, or <a href="https://example.com">the web</a>.</p>
  <p class="center">A centred line</p>
  <p style="text-align: right">A line on the right</p>
  <aside epub:type="footnote" id="fn1"><p>The mill was built in 1820 by the miller's grandfather.</p></aside>
  <hr/>
  <ul>
    <li>Flour</li>
    <li>Water
      <ol start="3"><li>cold</li><li>clean</li></ol>
    </li>
  </ul>
  <ul class="plain"><li>No bullet here</li></ul>
  <table>
    <tr><th>Grain</th><th>Price</th></tr>
    <tr><td><p>Wheat</p></td><td>12</td></tr>
  </table>
  <blockquote><p>Quoted words.</p></blockquote>
  <p>A star ★ the fonts lack.</p>
</body>
</html>"""

ENDNOTES = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Endnotes</title></head>
<body>
  <h2 id="part">Endnotes</h2>
  <section epub:type="endnotes">
    <ol>
      <li id="en1"><p>The first endnote.</p></li>
      <li id="en2"><p>Nobody knows; the parish records burned.</p><p>A second paragraph of it.</p></li>
    </ol>
  </section>
</body>
</html>"""


def main():
    with zipfile.ZipFile("extras.epub", "w") as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", OPF, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/nav.xhtml", NAV, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/css/book.css", CSS, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/images/hill.jpg", picture("JPEG", (600, 400), quality=85),
                   compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/images/dot.png", picture("PNG", (24, 24)), compress_type=zipfile.ZIP_STORED)
        z.writestr("OEBPS/images/full.jpg", picture("JPEG", (900, 1400), quality=80),
                   compress_type=zipfile.ZIP_STORED)
        z.writestr("OEBPS/images/progressive.jpg", picture("JPEG", (300, 200), progressive=True),
                   compress_type=zipfile.ZIP_STORED)
        z.writestr("OEBPS/text/pictures.xhtml", PICTURES, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/text/notes.xhtml", NOTES, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/text/endnotes.xhtml", ENDNOTES, compress_type=zipfile.ZIP_DEFLATED)
    print("wrote extras.epub")


if __name__ == "__main__":
    main()
