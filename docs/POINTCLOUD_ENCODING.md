# Pointcloud Encoding

## Wire representation

`active/{SN}/pointcloud` carries one `LidarPointMsgArray` per publication. The array contains one `LidarPointMsg`.

| Field | Encoding |
| --- | --- |
| `header.scaler` | Constant integer `1000` |
| `x`, `y`, `z` | `round(meters * 1000)`, signed integer millimeters |
| Decoded coordinate | `integer_xyz / 1000.0` meters |
| `rgbi` | XT-M60 per-point amplitude byte currently placed in the low value |
| `ring` | `0`; XT-M60 grid data does not currently provide a validated ring index |
| `offset` | `0`; no validated per-point temporal offset is available |

Points with NaN coordinates are not encoded. The sender retains only finite coordinates before millimeter conversion. The current grid frame has up to 9600 source samples; payload size varies with valid point count.

## Precision

Rounding introduces at most 0.5 mm of quantization error per coordinate before the receiving application divides by `1000.0`. `header.scaler` is deliberately constant because the customer decoder currently assumes that value.

## Validation required on live data

Acceptance should sample raw XT-M60 coordinates, emitted integer coordinates, and decoded meter coordinates from the same frame. It must record the maximum absolute coordinate error and any non-finite/empty-frame statistics. This document does not claim that a live sample run has been completed yet.
