"""Produce representative 3MF files using the installed writer, without a test suite."""

import json
from pathlib import Path
import sys
import uuid
import xml.etree.ElementTree as ET
from zipfile import ZipFile

import neroued_3mf as n3mf


CORE = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
PRODUCTION = "http://schemas.microsoft.com/3dmanufacturing/production/2015/06"
VENDOR = "https://example.org/3mf/production"
NS = {"c": CORE, "p": PRODUCTION}
LABEL = 'selected & "custom"\nparameter\tvalue\rretained'


def cube():
    mesh = n3mf.Mesh()
    mesh.vertices = [n3mf.Vec3f(*v) for v in (
        (0, 0, 0), (20, 0, 0), (20, 20, 0), (0, 20, 0),
        (0, 0, 20), (20, 0, 20), (20, 20, 20), (0, 20, 20),
    )]
    mesh.triangles = [n3mf.IndexTriangle(*t) for t in (
        (0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7),
        (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
        (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7),
    )]
    return mesh


def produce(mode, output):
    builder = n3mf.DocumentBuilder()
    builder.add_namespace("vendor", VENDOR)
    production = mode != "core"
    if production:
        builder.enable_production()
        builder.set_production_merge_objects(mode == "merged")
        builder.set_production_build_uuid(str(uuid.uuid4()))
    materials = builder.add_base_material_group([
        n3mf.BaseMaterial("Red", n3mf.Color.from_hex("#FF0000FF")),
        n3mf.BaseMaterial("Blue", n3mf.Color.from_hex("#0000FFFF")),
    ])
    objects = []
    for index in range(3):
        obj = builder.add_mesh_object(f"Part {index + 1}", cube(), materials, index % 2)
        objects.append(obj)
        if production:
            builder.set_object_uuid(obj, str(uuid.uuid4()))
            builder.set_component_uuid(obj, str(uuid.uuid4()))
            builder.set_component_transform(obj, n3mf.Transform.translation(index * 22, 0, 0))
    attributes = {"vendor:label": LABEL, "auto_drop": "1"}
    if mode == "grouped":
        for index, members in enumerate((objects[:2], objects[2:])):
            group = builder.add_production_group(f"Model {index + 1}", members, str(uuid.uuid4()))
            builder.add_build_item(group, n3mf.Transform.translation(30 + index * 50, 30, 0),
                                   f"model-{index + 1}", str(uuid.uuid4()), attributes)
    else:
        for obj in objects:
            builder.add_build_item(obj, attributes=attributes)
    layout = builder.plan_production_layout() if production else None
    doc = builder.build()
    n3mf.write_to_file(str(output), doc)
    with ZipFile(output) as archive:
        model_paths = [name for name in archive.namelist() if name.endswith(".model")]
        models = {name: ET.fromstring(archive.read(name)) for name in model_paths}
        items = models["3D/3dmodel.model"].findall("c:build/c:item", NS)
        observations = {
            "mode": mode, "file": output.name, "model_parts": model_paths,
            "root_items": [item.attrib for item in items],
            "material_resources": {
                name: [group.attrib["id"] for group in root.findall("c:resources/c:basematerials", NS)]
                for name, root in models.items()
            },
            "layout": None if layout is None else {
                "assembly_id": layout.assembly_id,
                "objects": [{"id": obj.object_id, "path": obj.path} for obj in layout.external_objects],
                "groups": [{"id": group.assembly_id, "path": group.path} for group in layout.groups],
            },
        }
        for item in items:
            if item.attrib.get(f"{{{VENDOR}}}label") != LABEL or item.attrib.get("auto_drop") != "1":
                raise RuntimeError("Installed writer changed caller-supplied attributes")
        for name, root in models.items():
            resources = observations["material_resources"][name]
            for obj in root.findall("c:resources/c:object", NS):
                if "pid" in obj.attrib and obj.attrib["pid"] not in resources:
                    raise RuntimeError(f"Unresolved material resource in {name}")
        if layout is not None:
            for obj in layout.external_objects:
                if obj.path.lstrip("/") not in models:
                    raise RuntimeError("Planned external model path was not written")
        return observations


destination = Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)
receipt = {"writer_version": n3mf.__version__, "exports": [
    produce(mode, destination / f"{mode}.3mf")
    for mode in ("core", "per-object", "merged", "grouped")
]}
(destination / "export-receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
print(json.dumps({"version": receipt["writer_version"],
                  "exports": [{"mode": row["mode"], "parts": len(row["model_parts"]),
                               "items": len(row["root_items"])} for row in receipt["exports"]]}))
