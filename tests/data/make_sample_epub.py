#!/usr/bin/env python3
"""Writes sample.epub and sample-ncx.epub, the small EPUBs the host tests read.

They exercise what real books do: a stored mimetype, deflated chapters, a
package file in a subfolder, an href with an escaped space and "../", a
navigation document outside the reading order with a nested entry that
links into the middle of a chapter, entities, headings, scripts and line
breaks. sample-ncx.epub is the same book with an EPUB 2 NCX table of
contents instead.
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
  <p id="poem">Eilėraštis:<br/>pirma eilė<br/>antra eilė</p>
  <hr/>
  <div><p>Paskutinis &#x201E;sakinys&#x201C;.</p></div>
</body>
</html>"""

NAV = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<body>
  <nav epub:type="landmarks"><ol><li><a href="text/ch1.xhtml">Landmark, not contents</a></li></ol></nav>
  <nav epub:type="toc" id="toc">
    <h1>Turinys</h1>
    <ol>
      <li><a href="text/ch1.xhtml"><span>Pirmas</span> skyrius</a>
        <ol><li><a href="text/ch1.xhtml#poem">Eilėraštis &amp; daina</a></li></ol>
      </li>
      <li><a href="text/Chapter%202.xhtml">Second</a></li>
      <li><a href="missing.xhtml">Not in the book</a></li>
    </ol>
  </nav>
</body>
</html>"""

NCX = """<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <navMap>
    <navPoint id="n1" playOrder="1">
      <navLabel><text>Pirmas skyrius</text></navLabel>
      <content src="text/ch1.xhtml"/>
      <navPoint id="n2" playOrder="2">
        <navLabel><text>Eilėraštis</text></navLabel>
        <content src="text/ch1.xhtml#poem"/>
      </navPoint>
    </navPoint>
    <navPoint id="n3" playOrder="3">
      <navLabel><text>Second</text></navLabel>
      <content src="text/Chapter%202.xhtml"/>
    </navPoint>
  </navMap>
</ncx>"""

CH2 = """<html><body><h2>Second</h2><p>Only paragraph.</p></body></html>"""

OPF_NCX = OPF.replace('version="3.0"', 'version="2.0"').replace(
    '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>',
    '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>').replace("<spine>", '<spine toc="ncx">')


def write(name, opf, toc_name, toc):
    with zipfile.ZipFile(name, "w") as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", opf, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/" + toc_name, toc, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/style.css", "p { text-indent: 1em }", compress_type=zipfile.ZIP_STORED)
        z.writestr("OEBPS/text/ch1.xhtml", CH1, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/text/Chapter 2.xhtml", CH2, compress_type=zipfile.ZIP_DEFLATED)
    print("wrote", name)


write("sample.epub", OPF, "nav.xhtml", NAV)
write("sample-ncx.epub", OPF_NCX, "toc.ncx", NCX)
