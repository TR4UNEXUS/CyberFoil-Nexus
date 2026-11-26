"""Build the two release assets from a verified CyberFoil Nexus NRO."""
from pathlib import Path
import re
import shutil
import struct
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
source, output = map(Path, sys.argv[1:])
version = re.search(r"^APP_VERSION\s*:=\s*(\d+\.\d+\.\d+)\s*$", (root / "Makefile").read_text(), re.M)[1]
image = source.read_bytes()
aset = struct.unpack_from("<I", image, 0x18)[0]
offset, size = struct.unpack_from("<QQ", image, aset + 24)
nacp = aset + offset
if (image[16:20] != b"NRO0" or image[aset:aset+4] != b"ASET" or size != 0x4000 or
    image[nacp:nacp+512].split(b"\0", 1)[0] != b"CyberFoil Nexus" or
    image[nacp+0x3060:nacp+0x3070].split(b"\0", 1)[0] != version.encode() or
    b"https://api.github.com/repos/TR4UNEXUS/CyberFoil-Nexus/releases/latest" not in image):
    raise ValueError("The input must be the Nexus updater build for the version in the Makefile")
output.mkdir(parents=True, exist_ok=True)
binary = output / "CyberFoil-Nexus.nro"
shutil.copyfile(source, binary)
base = "switch/CyberFoil-Nexus/"
with zipfile.ZipFile(output / f"CyberFoil-Nexus-{version}.zip", "w", zipfile.ZIP_DEFLATED) as package:
    package.write(binary, base + binary.name)
    for license in ("LICENSE", "awoo.LICENSE", "Tinfoil.LICENSE", "lib.LICENSE", "libusbhsfs.LICENSE"):
        package.write(root / license, base + "licenses/" + license)
    for dependency in ("external/libhaze", "include/Plutonium"):
        package.write(root / dependency / "LICENSE", base + "licenses/" + Path(dependency).name + ".LICENSE")
