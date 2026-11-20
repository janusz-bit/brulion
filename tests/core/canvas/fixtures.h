// Canvas files as their writers make them: Obsidian (JSON.stringify with tabs, one compact node per
// line, keys in Obsidian's order), the JSON Canvas spec's sample, a pretty-printed file of another
// tool (JSON.stringify(canvas, null, 2)), Obsidian with the Advanced Canvas plugin (unknown keys
// on nodes, edges and the root), and a few odd but valid ones.
#pragma once

#include <QString>

namespace canvasfixtures {

using namespace Qt::StringLiterals;

/// Obsidian 1.x: a group, a text card (Markdown, a task, a link, a tag), a note, a note's
/// section, a picture, a web page; edges with sides, an end without an arrow, an arrow at the
/// start, a colour and a label.
inline QString obsidian()
{
    return uR"js({
	"nodes":[
		{"id":"5b4c2f0e9a1d3c7e","type":"group","x":-420,"y":-320,"width":1380,"height":660,"label":"Remont kuchni"},
		{"id":"a1b2c3d4e5f60718","type":"text","text":"# Plan\n\n- [ ] zamówić płytki\n- kupić **farbę** do łazienki\n\nZobacz [[Budżet domowy]] i #remont","x":-400,"y":-280,"width":320,"height":240,"color":"4"},
		{"id":"0f1e2d3c4b5a6978","type":"file","file":"Projekty/Kuchnia.md","x":-40,"y":-280,"width":400,"height":400},
		{"id":"1122334455667788","type":"file","file":"Projekty/Kuchnia.md","subpath":"#Szafki","x":-40,"y":160,"width":400,"height":140,"color":"#ff8800"},
		{"id":"99aabbccddeeff00","type":"file","file":"assets/płytki.jpg","x":-400,"y":0,"width":320,"height":240},
		{"id":"abcdefabcdefabcd","type":"link","url":"https://example.com/żółw","x":520,"y":-280,"width":400,"height":200}
	],
	"edges":[
		{"id":"e1e1e1e1e1e1e1e1","fromNode":"a1b2c3d4e5f60718","fromSide":"right","toNode":"0f1e2d3c4b5a6978","toSide":"left","label":"szczegóły"},
		{"id":"e2e2e2e2e2e2e2e2","fromNode":"0f1e2d3c4b5a6978","fromSide":"bottom","toNode":"1122334455667788","toSide":"top","toEnd":"none","color":"2"},
		{"id":"e3e3e3e3e3e3e3e3","fromNode":"99aabbccddeeff00","fromSide":"top","fromEnd":"arrow","toNode":"a1b2c3d4e5f60718","toSide":"bottom"}
	]
})js"_s;
}

/// The spec's sample.canvas (github.com/obsidianmd/jsoncanvas).
inline QString specSample()
{
    return uR"js({
	"nodes":[
		{"id":"754a8ef995f366bc","type":"group","x":-300,"y":-460,"width":610,"height":200,"label":"JSON Canvas"},
		{"id":"8132d4d894c80022","type":"file","file":"readme.md","x":-280,"y":-200,"width":570,"height":560,"color":"6"},
		{"id":"7efdbbe0c4742315","type":"file","file":"_site/logo.svg","x":-280,"y":-440,"width":217,"height":80},
		{"id":"59e896bc8da20699","type":"text","text":"Learn more:\n\n- [Apps](/docs/apps.md)\n- [Spec](spec/1.0.md)\n- [Github](https://github.com/obsidianmd/jsoncanvas)","x":40,"y":-440,"width":250,"height":160},
		{"id":"0ba565e7f30e0652","type":"file","file":"spec/1.0.md","x":360,"y":-400,"width":400,"height":400}
	],
	"edges":[
		{"id":"6fa11ab87f90b8af","fromNode":"7efdbbe0c4742315","fromSide":"right","toNode":"59e896bc8da20699","toSide":"left"}
	]
}
)js"_s;
}

/// Another tool's JSON.stringify(canvas, null, 2): two spaces, a key per line, a newline at the
/// end.
inline QString pretty()
{
    return uR"js({
  "nodes": [
    {
      "id": "t1",
      "type": "text",
      "text": "Zażółć gęślą jaźń",
      "x": 0,
      "y": 0,
      "width": 250,
      "height": 60
    },
    {
      "id": "t2",
      "type": "text",
      "text": "Druga karta",
      "x": 400,
      "y": 0,
      "width": 250,
      "height": 60,
      "color": "1"
    }
  ],
  "edges": [
    {
      "id": "e1",
      "fromNode": "t1",
      "toNode": "t2"
    }
  ]
}
)js"_s;
}

/// Obsidian with the Advanced Canvas plugin: styleAttributes, zIndex, a portal, the root's
/// metadata — keys Brulion does not know, kept.
inline QString advanced()
{
    return uR"js({
	"metadata":{"version":"1.0-1.0","frontmatter":{"tags":["projekt"]},"startNode":"n1"},
	"nodes":[
		{"id":"n1","type":"text","text":"Start","styleAttributes":{"shape":"pill","border":"dashed"},"x":0,"y":0,"width":200,"height":80,"zIndex":3},
		{"id":"n2","type":"file","file":"Inny.canvas","portalToFile":"Inny.canvas","styleAttributes":{},"x":300,"y":0,"width":600,"height":400},
		{"id":"n3","type":"diagram","text":"(typ, którego Brulion nie zna)","x":0,"y":200,"width":200,"height":100}
	],
	"edges":[
		{"id":"x1","styleAttributes":{"path":"dotted","pathfindingMethod":"square"},"fromNode":"n1","fromSide":"right","toNode":"n2","toSide":"left"}
	]
})js"_s;
}

/// A file with a BOM, CRLF line endings and spaces after the colons.
inline QString crlfBom()
{
    return u"\uFEFF{\r\n  \"nodes\": [\r\n    {\"id\": \"a\", \"type\": \"text\", \"text\": \"ą\", \"x\": 1, \"y\": 2, "
           u"\"width\": 250, \"height\": 60}\r\n  ],\r\n  \"edges\": []\r\n}\r\n"_s;
}

// JSON escapes (kept out of the tests' sources: moc misreads raw strings with \" in them).
inline QString escaped()
{
    return uR"js({"nodes":[{"id":"q","type":"text","text":"\"cudzysłów\" \\ \/ \t\n\u0105 \ud83d\ude00 \ud800","x":0,"y":0,"width":10,"height":10}]})js"_s;
}
inline QString escapedShort()
{
    return uR"js({"nodes":[{"id":"q","type":"text","text":"\"a\" \\ \/ \t\n\u0105 \ud83d\ude00","x":0,"y":0,"width":10,"height":10}]})js"_s;
}
inline QString escapedWritten()
{
    return uR"js({"nodes":[{"id":"q","type":"text","text":"\"b\" \\ \t\nżółw 😀","x":0,"y":0,"width":10,"height":10}]})js"_s;
}

} // namespace canvasfixtures
