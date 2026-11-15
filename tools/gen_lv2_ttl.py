#!/usr/bin/env python3
"""Generates the LV2 Turtle metadata from the port table, so the TTL and the
plugin cannot drift apart."""
import os

URI = "https://github.com/tylorismusic-commits/comp-suite"

# (name, default, min, max, kind, group, unit, points)
PARAMS = [
    ("model", "Model", 0, 0, 5, "integer", "Compressor", None,
     [("76Blue", 0), ("76Black", 1), ("2A", 2), ("3A", 3), ("Xpress", 4), ("VCA", 5)]),
    ("power", "Power", 1, 0, 1, "toggle", "Compressor", None, []),
    ("input", "Input", 0.6, 0, 1, "control", "Compressor", None, []),
    ("threshold", "Threshold", 0.5, 0, 1, "control", "Compressor", None, []),
    ("ratio", "Ratio", 3, 0, 8, "integer", "Compressor", None, []),
    ("attack", "Attack", 0.5, 0, 1, "control", "Compressor", None, []),
    ("release", "Release", 0.5, 0, 1, "control", "Compressor", None, []),
    ("makeup", "Makeup", 0.5, 0, 1, "control", "Compressor", None, []),
    ("output", "Output", 0.5, 0, 1, "control", "Output", None, []),
    ("mix", "Mix", 1.0, 0, 1, "control", "Output", None, []),
    ("sc_hpf", "Sidechain HPF", 0, 0, 1, "control", "Sidechain", None, []),
    ("distortion", "Distortion", 0, 0, 3, "integer", "Output", None,
     [("Off", 0), ("Dist 2", 1), ("Dist 3", 2), ("Clip", 3)]),
    ("meter", "Meter", 0, 0, 4, "integer", "Meter", None,
     [("GR", 0), ("+8", 1), ("+4", 2), ("Graph", 3), ("Off", 4)]),
    ("limiter_on", "Limiter", 0, 0, 1, "toggle", "Limiter", None, []),
    ("limiter_threshold", "Limiter Threshold", 0.5, 0, 1, "control", "Limiter", None, []),
]

OUTPUTS = [
    ("gr_meter", "Gain Reduction", "Meter"),
    ("out_peak", "Output Peak", "Meter"),
    ("in_peak", "Input Peak", "Meter"),
    ("out_vu", "Output VU", "Meter"),
]


def port(index, symbol, name, minimum, maximum, default, kind, group,
         unit, points, is_output=False):
    lines = [
        "    [",
        f"        a lv2:Port , lv2:{'OutputPort' if is_output else 'InputPort'} , lv2:ControlPort ;",
        f'        lv2:index {index} ;',
        f'        lv2:symbol "{symbol}" ;',
        f'        lv2:name "{name}" ;',
        f"        lv2:default {default} ;",
        f"        lv2:minimum {minimum} ;",
        f"        lv2:maximum {maximum} ;",
    ]
    if kind in ("integer", "toggle"):
        lines.append("        lv2:integer true ;")
    if kind == "toggle":
        lines.append("        lv2:toggled true ;")
    if points:
        pts = " ,\n            ".join(
            f'[ lv2:value {v} ; rdfs:label "{label}" ]' for label, v in points)
        lines.append(f"        lv2:scalePoint {pts} ;")
    if unit:
        lines.append(f"        lv2:designation units:{unit} ;")
    lines.append(f'        pg:group <{URI}#{group.lower()}> ;')
    lines.append("    ] ,")
    return "\n".join(lines)


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ports = [
        "    [",
        "        a lv2:AudioPort , lv2:InputPort ;",
        "        lv2:index 0 ;",
        '        lv2:symbol "in_l" ;',
        '        lv2:name "Input Left" ;',
        "    ] ,",
        "    [",
        "        a lv2:AudioPort , lv2:InputPort ;",
        "        lv2:index 1 ;",
        '        lv2:symbol "in_r" ;',
        '        lv2:name "Input Right" ;',
        "    ] ,",
        "    [",
        "        a lv2:AudioPort , lv2:OutputPort ;",
        "        lv2:index 2 ;",
        '        lv2:symbol "out_l" ;',
        '        lv2:name "Output Left" ;',
        "    ] ,",
        "    [",
        "        a lv2:AudioPort , lv2:OutputPort ;",
        "        lv2:index 3 ;",
        '        lv2:symbol "out_r" ;',
        '        lv2:name "Output Right" ;',
        "    ] ,",
    ]
    index = 4
    for p in PARAMS:
        ports.append(port(index, p[0], p[1], p[3], p[4], p[2], p[5], p[6],
                          p[7], p[8]))
        index += 1
    for o in OUTPUTS:
        # minimum, maximum, default - meters are dB and can be negative.
        ports.append(port(index, o[0], o[1], -120, 24, 0, "control", o[2],
                          "db", [], True))
        index += 1
    ports[-1] = ports[-1].rstrip(" ,")

    ttl = f"""@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix doap: <http://usefulinc.com/ns/doap#> .
@prefix units: <http://lv2plug.in/ns/extensions/units#> .
@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .
@prefix pg:   <http://lv2plug.in/ns/ext/port-groups#> .
@prefix foaf: <http://xmlns.com/foaf/0.1/> .

<{URI}>
    a lv2:Plugin , lv2:CompressorPlugin ;
    doap:name "Comp Suite" ;
    doap:shortdesc "Six compressor models in one plugin" ;
    doap:description "76Blue, 76Black, 2A, 3A, Xpress and VCA, with a shared ratio ladder, parallel mix and an output limiter."@en ;
    doap:license <https://spdx.org/licenses/GPL-3.0-or-later.html> ;
    doap:maintainer [ foaf:name "tylorismusic" ] ;
    lv2:binary <comp_suite.so> ;
    rdfs:seeAlso <manifest.ttl> ;
    ui:ui <{URI}#ui> ;
    lv2:optionalFeature lv2:hardRTCapable ;
    lv2:port
{chr(10).join(ports)} .

<{URI}#compressor> a pg:Group ; lv2:name "Compressor" .
<{URI}#output> a pg:Group ; lv2:name "Output" .
<{URI}#sidechain> a pg:Group ; lv2:name "Sidechain" .
<{URI}#limiter> a pg:Group ; lv2:name "Limiter" .
<{URI}#meter> a pg:Group ; lv2:name "Meter" .
"""
    with open(os.path.join(here, "lv2", "comp_suite.ttl"), "w") as f:
        f.write(ttl)

    manifest = f"""@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .

<{URI}>
    a lv2:Plugin ;
    lv2:binary <comp_suite.so> ;
    rdfs:seeAlso <comp_suite.ttl> ;
    ui:ui <{URI}#ui> .

<{URI}#ui>
    a ui:X11UI ;
    ui:binary <comp_suite_ui.so> ;
    lv2:requiredFeature ui:parent ;
    rdfs:seeAlso <comp_suite.ttl> .
"""
    with open(os.path.join(here, "lv2", "manifest.ttl"), "w") as f:
        f.write(manifest)


if __name__ == "__main__":
    main()
