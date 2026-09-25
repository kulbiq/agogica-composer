# /// script
# requires-python = ">=3.10"
# dependencies = ["h5py", "numpy", "scipy"]
# ///
"""Builds the binaural HRIR tables embedded in the plugin (Resources/hrir/*.bin).

Source: SADIE II database, KU100 dummy head (subject D1), University of York,
Apache-2.0 - https://zenodo.org/records/12092466 (D1_HRIR_SOFA.zip).

    uv run tools/make_hrir.py build/hrtf-src/D1_HRIR_SOFA

Only needed to regenerate the tables; the plugin build uses the committed .bin files.

What it does (see Resources/hrir/NOTICE):
- takes the 0-degree-elevation ring (1-degree grid) and re-indexes it in Composer's
  convention: index i = azimuth i degrees clockwise (+90 = right). SOFA is
  counter-clockwise (+90 = left), so SOFA azimuth = -i;
- strips one global onset (the earliest onset over all directions and both ears), so
  interaural time differences are kept exactly;
- truncates with a short fade-out;
- equalises the ~0.2 dB sensitivity difference between the two ear microphones;
- resamples the native 44.1/96 kHz sets to 88.2/176.4/192 kHz;
- scales each table so the front (0 degree) pair carries energy 0.5 per ear, i.e. the
  same level as the constant-power stereo pan at centre.

Output (little-endian): b"HRIR", u32 version=1, u32 count=360, u32 taps, f32 sampleRate,
then float32 [count][2 ears: left, right][taps].
"""

import pathlib
import struct
import sys

import h5py
import numpy as np
from scipy.signal import resample_poly

OUT = pathlib.Path(__file__).resolve().parent.parent / "Resources" / "hrir"
TAPS_AT_48K = 256


def load_ring(path: pathlib.Path):
    with h5py.File(path) as f:
        pos = f["SourcePosition"]
        assert pos.attrs["Type"] in (b"spherical", "spherical"), "expected spherical source positions"
        positions = pos[:]
        ir = f["Data.IR"][:]
        rate = float(f["Data.SamplingRate"][0])

    ring = np.flatnonzero(np.abs(positions[:, 1]) < 0.01)  # elevation 0, by value
    sofa_azimuth = positions[ring, 0] % 360.0

    table = np.empty((360, 2, ir.shape[2]))
    for i in range(360):
        wanted = (-i) % 360  # Composer clockwise -> SOFA counter-clockwise
        distance = np.abs((sofa_azimuth - wanted + 180.0) % 360.0 - 180.0)
        nearest = ring[np.argmin(distance)]
        assert distance.min() < 0.51, f"no measurement near SOFA azimuth {wanted}"
        table[i] = ir[nearest]

    # Receiver 0 must be the left ear: at +90 (right) the right ear is louder.
    energy = (table[90] ** 2).sum(axis=1)
    assert energy[1] > 4 * energy[0], "ear order or azimuth sign is wrong"
    return table, rate


def onset(ir: np.ndarray) -> int:
    peak = np.abs(ir).max()
    return int(np.argmax(np.abs(ir) > 0.1 * peak)) if peak > 0 else ir.shape[-1]


def finish(table: np.ndarray, rate: float) -> np.ndarray:
    scale = rate / 48000.0
    taps = int(round(TAPS_AT_48K * scale))

    start = min(onset(table[i, ear]) for i in range(360) for ear in range(2))
    start = max(0, start - int(round(2 * scale)))  # keep a little pre-ringing
    table = table[:, :, start:start + taps]
    if table.shape[2] < taps:
        table = np.pad(table, ((0, 0), (0, 0), (0, taps - table.shape[2])))

    fade = int(round(16 * scale))
    table[:, :, -fade:] *= 0.5 + 0.5 * np.cos(np.linspace(0.0, np.pi, fade))

    # The two ear microphones differ by ~0.2 dB over all directions: equalise that
    # (a per-ear gain) and keep every direction's measured detail.
    diffuse = (table ** 2).sum(axis=2).mean(axis=0)
    table = table * np.sqrt(diffuse.mean() / diffuse)[None, :, None]

    front = (table[0] ** 2).sum(axis=1).mean()
    return (table * np.sqrt(0.5 / front)).astype("<f4")


def write(table: np.ndarray, rate: float):
    name = f"sadie2_ku100_{int(round(rate))}.bin"
    header = b"HRIR" + struct.pack("<3If", 1, table.shape[0], table.shape[2], rate)
    (OUT / name).write_bytes(header + table.tobytes())
    print(f"{name}: {table.shape[0]} directions x {table.shape[2]} taps")


def main(source: pathlib.Path):
    OUT.mkdir(parents=True, exist_ok=True)
    native = {
        44100: load_ring(next(source.glob("D1_44K_*.sofa"))),
        48000: load_ring(next(source.glob("D1_48K_*.sofa"))),
        96000: load_ring(next(source.glob("D1_96K_*.sofa"))),
    }

    for rate, (table, measured) in native.items():
        assert measured == rate
        write(finish(table, rate), rate)

    for rate, base, factor in ((88200, 44100, 2), (176400, 44100, 4), (192000, 96000, 2)):
        upsampled = resample_poly(native[base][0], factor, 1, axis=2)
        write(finish(upsampled, rate), rate)


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1]))
