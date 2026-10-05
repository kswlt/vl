# Local calibration tool

This is a local-only capture/solve utility for the physical 1400 x 1000 mm board. It does not SSH to, write files to, compile for, or deploy to the RK board.

The board uses marker IDs 1-4 in the four corners. `local_calibrate.py` uses OpenCV `DICT_6X6_250`, 200 mm markers, and the coordinates in `HANDOFF_LOCAL_CALIBRATION_20260929.md`. The provided marker PDFs don't contain the dictionary capacity; the four IDs are decodable in the OpenCV 6x6 dictionaries tested. Check the live capture overlay before collecting.

## Capture

Connect the PC's wired Ethernet adapter to the RK management port or the same switch. Set that adapter's IPv4 to `198.51.100.200`, subnet mask `255.255.255.0`, and leave gateway/DNS blank. In Windows use `Win+R` → `ncpa.cpl` → the connected Ethernet adapter → Properties → IPv4. Do not change Wi-Fi or the separate camera/radar sensor adapter. If using a different unused host address, change the endpoint bind address below to match.

Check the route and Zenoh port in PowerShell:

```powershell
ping 198.51.100.179
Test-NetConnection 198.51.100.179 -Port 7447
```

The RK management page is `http://198.51.100.179:8080/`. The tool receives camera pictures from the RK's Zenoh `active/DEVICE00000001/image` topic and point clouds from `active/DEVICE00000001/pointcloud`; it does not connect directly to the MC800S RTSP stream. Both topics use one Zenoh session.

The collector accepts the device's current JPEG or H.264 protocol output and saves a decoded 1920 x 1080 frame with the point cloud. No stream-format change is needed. Start it from the project root:

```powershell
.\.venv\Scripts\python.exe tools\calibration\local_calibrate.py capture --output calib_data_20260929
```

In the capture window, wait for the board to stop moving for at least 2 seconds, then press Space to save a pose. Press Q to quit. The collector matches the camera frame to the nearest LiDAR scan by device timestamp and rejects pairs over 150 ms apart. It saves that one scan, not a time-mixed one-second cloud. Keep all four markers visible when possible.

Collect 20–30 poses with **large changes in the board normal**: tilt the top edge toward/away from the sensors and tilt the left/right edges, at different distances and image locations. Translation of the board or rotation around its own normal does not make plane-based translation observable. The solver rejects datasets whose normal coverage is too weak.

If it stays on “Waiting for image”, verify `Test-NetConnection` succeeds, the endpoint bind IP is assigned to the connected PC adapter, and the SN is correct. Saved files are on the PC, never on the RK board.

The tool rejects an existing non-empty output folder. Do not run several copies of the collector against the same output folder.

H.264 capture locally decodes a recent GOP to select the saved frame. It does not change the protocol stream or board software.

## Solve

```powershell
.\.venv\Scripts\python.exe tools\calibration\local_calibrate.py solve --dataset calib_data_20260929 --min-poses 12 --sn DEVICE00000001 --baseline-m <实测米数> --max-baseline-m <物理上限米数>
```

Results go under `calib_data_20260929/result_guided/`. Inspect `report.json`, rejected poses, held-out pose errors, and the projection images in `diagnostics/`. Blue/cyan points should lie on the physical board. The solver uses board normals and signed plane offsets; it does not treat the observed point-cloud centroid as the board center. It checks the supplied physical baseline and pose-normal coverage. A failed observability or baseline check does not establish a valid extrinsic; investigate point association and geometry before recapturing.

There is no default baseline: the earlier 0.025 m prior and 0.06 m limit came from an unverified 2–3 cm claim. Front photographs with a ruler indicate a 7–8 cm separation between visible window/lens centers; the historical file states `t=[0,-0.07511,-0.0189]` m, but the internal LiDAR origin and depth offset remain unverified. Supply a measured origin-to-origin magnitude and justified limit. Keep the generated candidate `valid=false` until independent board poses and physical layout review pass.

The October 3 captures were made before the board-side Y sign correction. Add `--legacy-mirrored-y` when solving only those old captures; the correction happens in memory and is recorded in the report. New captures made after deploying a corrected board build must omit this flag. A low plane residual does not establish physical baseline accuracy: compare the freely calibrated camera intrinsics with independent images and measure at least two known board distances before acceptance.

Validate any frozen candidate on a **separate capture session**, including any
single pose with a tape measurement. The solver's every-fifth-pose holdout
shares the same session conditions and can miss a session-level bias:

```powershell
.\.venv\Scripts\python.exe tools\calibration\validate_candidate.py --candidate calib_data_20261003_pitch_reverse_test\result_legacy_y_fixed_provisional\calibration_candidate.json --dataset calib_data_20261004_one_distance_check --legacy-mirrored-y --output calib_data_20261004_one_distance_check\validation_against_reverse_candidate.json
```

The validator uses a 3 cm independent board-plane diagnostic threshold; it
does not certify the physical sensor origin or change any `valid` field. The
2026-10-04 measured-distance capture fails the October 3 candidate with an
11.4 cm board-plane RMS, so that candidate must not be deployed.

## Limitations

- Capture accepts protocol JPEG or H.264.
- Camera intrinsics are estimated from sparse four-marker observations. Collect many varied views and confirm the residuals.
- LiDAR plane extraction uses the detected camera board location and normal to gate candidate points before RANSAC. The axis hint follows the published X-forward/Y-left/Z-up cloud; the October 3 scans put floor returns at negative Z, consistent with this convention. Revalidate if the output coordinate mode changes.
- The solver rejects unsynchronized pairs, fits from plane normals and signed offsets, checks translation observability, checks the measured baseline, and evaluates held-out poses.
- Existing captures that merged several scans can recover the nearest scan only when their metadata contains per-scan counts and timestamps. If not, recollect them.
- No board-side source or calibration file is modified by this tool.
