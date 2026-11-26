"""Downloads the Agogica Tracker's models for development builds; the files are not committed -
see MODELS.md for sources, licences and training-data terms.

    python3 tracker/models/fetch_models.py

models.json lists them (name, SHA-256, archive, the file inside it). The archives are streamed
and abandoned as soon as the needed file has been read, so only a fraction of each is
downloaded. A download that doesn't match its pinned hash is not kept.
"""

import hashlib
import json
import pathlib
import tarfile
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent


def fetch(name: str, sha256: str, url: str, archive_member: str):
    target = HERE / name
    if target.exists():
        return
    print(f"{name}: downloading…", flush=True)
    data = None
    with urllib.request.urlopen(url, timeout=60) as response:  # a stalled server must not hang the build
        with tarfile.open(fileobj=response, mode="r|gz") as archive:
            for entry in archive:
                if entry.name.endswith(archive_member):
                    data = archive.extractfile(entry).read()
                    break
    if data is None:
        raise SystemExit(f"{archive_member} not found in {url}")
    if hashlib.sha256(data).hexdigest() != sha256:
        raise SystemExit(f"{name}: the download doesn't match the SHA-256 pinned in models.json")
    partial = target.with_suffix(".part")
    partial.write_bytes(data)
    partial.rename(target)
    print(f"{name}: {len(data) / 1e6:.1f} MB")


if __name__ == "__main__":
    for model in json.loads((HERE / "models.json").read_text()):
        fetch(model["name"], model["sha256"], model["url"], model["archiveMember"])
