import schemdraw
import schemdraw.elements as elm

schemdraw.use("svg")

PINMAP = {"DHT": 21, "BTN": 4, "FAN": 25, "R": 26, "G": 27, "B": 14, "CLK": 18, "DIO": 19}
FS = 9  # label font size

# y of each GPIO pin == y of the component row it feeds (top to bottom)
ROW_Y = {"DHT": 26, "BTN": 18, "FAN": 6, "R": -2, "G": -4.5, "B": -7, "CLK": -13, "DIO": -14.5}
BOX_X0, BOX_X1, BOX_Y0, BOX_Y1 = 0, 6, -18, 28
TX = 16  # x where every component row starts

with schemdraw.Drawing(show=False) as d:
    d.config(unit=3, fontsize=10, lw=1.5)

    def wire(*pts):
        for a, b in zip(pts, pts[1:]):
            d.add(elm.Line().at(a).to(b))

    def dot(p):
        d.add(elm.Dot().at(p))

    def label(p, text, **kw):
        d.add(elm.Label().at(p).label(text, fontsize=kw.pop("fontsize", FS), **kw))

    def vdd(p, text):
        d.add(elm.Vdd().at(p).label(text, fontsize=FS))

    def gnd(p):
        d.add(elm.Ground().at(p))

    def box(x0, y0, x1, y1):
        wire((x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0))

    box(BOX_X0, BOX_Y0, BOX_X1, BOX_Y1)
    label(((BOX_X0 + BOX_X1) / 2, (BOX_Y0 + BOX_Y1) / 2), "ESP32", fontsize=10)
    for k, y in ROW_Y.items():
        label((BOX_X1 - 0.3, y), f"GPIO{PINMAP[k]}", halign="right", fontsize=10)
        wire((BOX_X1, y), (TX, y))

    for text, y in (("5V", 12), ("3V3", 10), ("GND", 8)):
        label((BOX_X0 + 0.3, y), text, halign="left", fontsize=10)
        wire((BOX_X0, y), (BOX_X0 - 1.5, y))
    vdd((BOX_X0 - 1.5, 12), "+5V")
    vdd((BOX_X0 - 1.5, 10), "+3V3")
    gnd((BOX_X0 - 1.5, 8))

    y = ROW_Y["DHT"]
    bx = TX + 3
    box(bx, y - 2.5, bx + 3.5, y + 2.5)
    label((bx + 1.75, y + 3.2), "DHT11")
    for name, dy in (("VCC", 1.5), ("DATA", 0), ("GND", -1.5)):
        label((bx + 0.3, y + dy), name, halign="left")
    rx = TX + 1.2
    wire((bx, y + 1.5), (rx, y + 1.5)); dot((rx, y + 1.5))
    vdd((rx, y + 1.5), "+3V3")
    d.add(elm.Resistor().at((rx, y + 1.5)).to((rx, y)).label("10k", loc="left", fontsize=FS))
    wire((TX, y), (rx, y)); dot((rx, y))
    wire((rx, y), (bx, y))
    wire((bx, y - 1.5), (bx - 1.5, y - 1.5)); gnd((bx - 1.5, y - 1.5))

    y = ROW_Y["BTN"]
    box(TX + 2, y - 1.8, TX + 5, y + 1.8)
    label((TX + 3.5, y + 2.5), "SW1")
    for name, x_edge, x_tie, dy in (("1", TX + 2, TX + 1, 0.75), ("2", TX + 2, TX + 1, -0.75),
                                    ("3", TX + 5, TX + 6, 0.75), ("4", TX + 5, TX + 6, -0.75)):
        wire((x_edge, y + dy), (x_tie, y + dy))
        label((x_edge + (0.4 if x_edge == TX + 2 else -0.4), y + dy), name)
    wire((TX + 1, y + 0.75), (TX + 1, y - 0.75)); wire((TX, y), (TX + 1, y)); dot((TX + 1, y))
    wire((TX + 6, y + 0.75), (TX + 6, y - 0.75)); wire((TX + 6, y), (TX + 7, y)); dot((TX + 6, y))
    gnd((TX + 7, y))

    y = ROW_Y["FAN"]
    d.add(elm.Motor().at((TX, y)).to((TX + 4, y)).label("FAN", loc="top", fontsize=FS))
    gnd((TX + 4, y))

    ys = []
    for k, val, col in (("R", "150", "red"), ("G", "100", "green"), ("B", "100", "blue")):
        y = ROW_Y[k]
        ys.append(y)
        d.add(elm.Resistor().at((TX, y)).to((TX + 3, y)).label(val, loc="top", fontsize=FS))
        d.add(elm.LED().at((TX + 3, y)).to((TX + 5.5, y)).color(col))
        wire((TX + 5.5, y), (TX + 6.5, y)); dot((TX + 6.5, y))
    wire((TX + 6.5, ys[0]), (TX + 6.5, ys[-1] - 1.0))
    gnd((TX + 6.5, ys[-1] - 1.0))
    label((TX + 3.5, ys[0] + 1.6), "RGB LED")

    bx = TX + 3
    box(bx, -17, bx + 6, -10.5)
    label((bx + 3, -9.5), "Gotek LEDC68")
    for name, y in (("VCC", -11.5), ("CLK", ROW_Y["CLK"]), ("DIO", ROW_Y["DIO"]), ("GND", -16)):
        label((bx + 0.3, y), name, halign="left")
    wire((bx, ROW_Y["CLK"]), (TX, ROW_Y["CLK"]))
    wire((bx, ROW_Y["DIO"]), (TX, ROW_Y["DIO"]))
    wire((bx, -11.5), (bx - 1.5, -11.5)); vdd((bx - 1.5, -11.5), "+5V")
    wire((bx, -16), (bx - 1.5, -16)); gnd((bx - 1.5, -16))

    label((TX + 3, BOX_Y1 + 2), "ESP32 controller", fontsize=14)
    d.save("esp32_controller.svg")
print("saved esp32_controller.svg")
