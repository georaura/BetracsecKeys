"""Añade (o reemplaza) una versión en el appcast de Sparkle.

uso: appcast.py <appcast.xml> <nombre> <versión> <url del .pkg> '<atributos de sign_update>' '<notas html>' <web> <macOS mínimo>

La descarga es un .pkg suelto (Sparkle lo instala con el instalador de macOS y así actualiza
también el AU y el VST3).
"""
import re, sys, os
from email.utils import formatdate

path, name, version, url, sig, notes, link, minos = sys.argv[1:9]
item = f"""    <item>
      <title>{name} {version}</title>
      <pubDate>{formatdate(localtime=True)}</pubDate>
      <sparkle:version>{version}</sparkle:version>
      <sparkle:shortVersionString>{version}</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>{minos}</sparkle:minimumSystemVersion>
      <description><![CDATA[{notes}]]></description>
      <enclosure url="{url}" type="application/octet-stream" {sig.strip()} />
    </item>
"""
head = f"""<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>{name}</title>
    <link>{link}</link>
    <language>es</language>
"""
tail = "  </channel>\n</rss>\n"

items = []
if os.path.exists(path):
    items = re.findall(r"    <item>.*?</item>\n", open(path, encoding="utf-8").read(), re.S)
    items = [i for i in items if f"<sparkle:version>{version}</sparkle:version>" not in i]
open(path, "w", encoding="utf-8").write(head + item + "".join(items) + tail)
print(f"appcast: {path} ({len(items) + 1} versiones)")
