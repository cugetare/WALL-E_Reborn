# Cost ledger

Currency totals remain separate; no exchange rate is frozen into the project.
`Paid` is cash spent, `allocated` is material assigned to this robot, and
`replication buy` is the supplier package another builder must actually order.

## Paid orders

### Waveshare order (USD)

| Item | SKU | Qty | Unit | Subtotal | Assignment |
|---|---:|---:|---:|---:|---|
| SC09 2.3 kg serial bus servo | 24440 | 7 | $8.31 | $58.17 | Baseline robot |
| SC15 17 kg serial bus servo | 21568 | 1 | $17.99 | $17.99 | Fallback/spare, not baseline |
| 5264-3PIN servo cable pack (6 pcs) | 34322 | 1 | $2.99 | $2.99 | Robot/inventory; length as ordered |
| 5264-3PIN servo cable pack (6 pcs) | 34171 | 2 | $1.99 | $3.98 | Robot/inventory; length as ordered |
| Metal flat key shaft flange plate | 24985 | 1 | $2.99 | $2.99 | Mechanical inventory |
| Serial Bus Servo Driver Board | 25514 | 1 | $4.99 | $4.99 | Baseline robot |
| ESP32-S3 N32R16V without headers | 32837 | 1 | $14.99 | $14.99 | Baseline robot |
| 1.69-inch 240x280 SPI LCD | 24382 | 1 | $9.49 | $9.49 | Baseline robot |
| **Merchandise subtotal** | | | | **$115.59** | |
| Shipping and handling | | | | **$15.00** | Order-level cost |
| **Cash paid** | | | | **$130.59** | |

Purchased Waveshare merchandise assigned to the current baseline is **$115.59**;
the SC15 is now the neck-tilt actuator rather than a fallback. Shipping is
kept as an order-level cost because a future quote can change with destination
and basket contents.

### Illuminated switch order (EUR)

| Item | Bought | Used | Paid per piece | Allocated to robot | Replication buy |
|---|---:|---:|---:|---:|---:|
| 6-pin illuminated momentary switch, 6x6x7.2 mm, mixed colours | 20 | 5 | EUR 0.5345 | **EUR 2.6725** | **EUR 10.69** |

The listing price includes VAT. Fifteen switches, representing EUR 8.0175 of
the lot, remain project inventory rather than robot content.

## Current totals

| View | USD | EUR |
|---|---:|---:|
| Cash paid so far | **$130.59** | **EUR 10.69** |
| Purchased baseline material assigned (before power parts) | **$115.59** | **EUR 2.6725** |
| Inventory not consumed | — | **EUR 8.0175 switches** |

## Planned / price TBD

| Qty | Item | Status | Cost |
|---:|---|---|---:|
| 1 | Pololu Big Pushbutton Power Switch HP #2813 | Selected, not recorded as purchased | TBD landed |
| **1** | **Additional SC09, SKU 24440** | Required: 8 total, only 7 purchased | TBD landed |
| 5 | LED series resistors, initial value 1 kohm | Direct motion/media S3 GPIO drive; confirm brightness after bench measurement | TBD |
| 1 | 3S pack + balancing BMS + charger | 3S architecture fixed; exact parts/owned items to record | TBD |
| 1 | INA226 plus suitable external shunt | Planned battery telemetry | TBD |
| 1 | 6 V SC09 regulator, 7.2 V SC15 regulator, fuse and distribution hardware | Pending measured current; existing H-bridge is owned | TBD |

When adding an order, record supplier, date, currency, product/SKU, quantity,
merchandise subtotal, tax, shipping and the number actually consumed by one
robot. Never convert historical purchases using today's exchange rate.
