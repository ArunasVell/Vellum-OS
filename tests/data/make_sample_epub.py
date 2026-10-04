#!/usr/bin/env python3
"""Writes sample.epub, the small EPUB the host tests read.

It exercises what real books do: a stored mimetype, deflated chapters, a
package file in a subfolder, an href with an escaped space and "../", a
navigation document outside the reading order, entities, headings, scripts
and line breaks.
"""
import zipfile

CONTAINER = """<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""

OPF = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="id">vellum-test</dc:identifier>
    <dc:title>Vellum &amp; the Test   Book</dc:title>
    <dc:creator>Arūnas Tester</dc:creator>
    <dc:language>lt</dc:language>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="css" href="style.css" media-type="text/css"/>
    <item id="c1" href="text/ch1.xhtml" media-type="application/xhtml+xml"/>
    <item id="c2" href="text/../text/Chapter%202.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine>
    <itemref idref="c1"/>
    <itemref idref="c2"/>
  </spine>
</package>"""

CH1 = """<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>Ignored</title><style>p { margin: 0 }</style></head>
<body>
  <h1 class="title">Pirmas   skyrius</h1>
  <p>Labas, <i>pasauli</i> &amp; draugai&#8212;sveiki!</p>
  <p>Ne&shy;lauž&shy;ti   tarpai&nbsp;čia.
     Antra eilutė.</p>
  <script>var hidden = "<p>not text</p>";</script>
  <!-- <p>a comment</p> -->
  <p>Eilėraštis:<br/>pirma eilė<br/>antra eilė</p>
  <hr/>
  <div><p>Paskutinis &#x201E;sakinys&#x201C;.</p></div>
</body>
</html>"""

CH2 = """<html><body><h2>Second</h2><p>Only paragraph.</p></body></html>"""

with zipfile.ZipFile("sample.epub", "w") as z:
    z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
    z.writestr("META-INF/container.xml", CONTAINER, compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("OEBPS/content.opf", OPF, compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("OEBPS/nav.xhtml", "<html><body><nav>Contents</nav></body></html>", compress_type=zipfile.ZIP_STORED)
    z.writestr("OEBPS/style.css", "p { text-indent: 1em }", compress_type=zipfile.ZIP_STORED)
    z.writestr("OEBPS/text/ch1.xhtml", CH1, compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("OEBPS/text/Chapter 2.xhtml", CH2, compress_type=zipfile.ZIP_DEFLATED)
print("wrote sample.epub")
