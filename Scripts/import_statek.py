"""Import the modular ship kit in UE 5.8.4.

Run from Unreal's Python console with py "path/to/Scripts/import_statek.py", or
use UnrealEditor-Cmd <uproject> -run=pythonscript -script=<script> -unattended.
Set STATEK_SOURCE to use a different source folder. STATEK_VERIFY_ONLY=1
requires every package to exist. Existing packages are always read-only.
"""

import json
import math
import os
from pathlib import Path

import unreal as ue


SOURCE = Path(os.environ.get("STATEK_SOURCE", Path.home() / "Documents" / "StatekFPP_Moduly"))
ROOT = "/Game/StatekFPP"
CONTENT = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_content_dir()))
VERIFY_ONLY = os.environ.get("STATEK_VERIFY_ONLY") == "1"
MAT = ue.MaterialEditingLibrary
PROP = ue.MaterialProperty
SAMPLE = ue.MaterialSamplerType
PACK = ue.TextureCompressionSettings

SURFACES = {
    "PaintDark": ("T_PaintDark_BC", "T_Paint_ORM", "T_Paint_N"),
    "PaintCargo": ("T_PaintCargo_BC", "T_Paint_ORM", "T_Paint_N"),
    "EdgeWear": ("T_EdgeWear_BC", "T_EdgeWear_ORM", "T_Paint_N"),
    "Hazard": ("T_Hazard_BC", "T_Paint_ORM", "T_Paint_N"),
    "BallShell": ("T_ServiceBall_BC", "T_ServiceBall_ORM", None),
}
SOLIDS = {
    "MetalDark": ("2B2E32", 0.8, 0.5),
    "Rubber": ("141517", 0.0, 0.85),
    "Marking": ("CFCAB8", 0.0, 0.55),
}
MESHES = (
    "SM_PanelWall_Face", "SM_PanelWall_Frame", "SM_PanelFloor_Face", "SM_PanelFloor_Frame",
    "SM_Guide_Rotor", "SM_Guide_Mount", "SM_Guide_Pocket", "SM_CargoCrate", "SM_ServiceBall",
)
TEXTURES = sorted({t for row in SURFACES.values() for t in row if t} | {"T_Indicator_Mask"})
TRANSLATOR_SETTINGS = {"convert_scene": True, "force_front_x_axis": False, "convert_scene_unit": True}


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_properties(obj, expected):
    for key, value in expected.items():
        actual = obj.get_editor_property(key)
        check(actual == value, f"{obj.get_name()}.{key}: expected {value}, got {actual}")


def package_file(package):
    return CONTENT / (package.removeprefix("/Game/") + ".uasset")


def get_asset(folder, name, cls, create, verify):
    package = f"{ROOT}/{folder}/{name}"
    disk_file = package_file(package)
    # The registry can still be scanning during a commandlet. Check disk as well.
    existing = disk_file.exists() or ue.find_object(None, f"{package}.{name}") is not None
    if existing:
        asset = ue.load_asset(package)
    else:
        check(not VERIFY_ONLY, f"Missing package: {package}")
        asset = create()
    check(isinstance(asset, cls) and asset.get_path_name() == f"{package}.{name}",
          f"Expected {cls.__name__} at {package}, got {asset}")
    verify(asset)
    if not existing:
        check(not disk_file.exists(), f"Package appeared during import: {disk_file}")
        check(ue.EditorLoadingAndSavingUtils.save_packages([asset.get_outermost()], False),
              f"Could not save {package}")
    ue.log(f"StatekFPP {'verified' if existing else 'created'} {package}")
    return asset


def import_one(source, folder, pipeline):
    manager = ue.InterchangeManager.get_interchange_manager_scripted()
    src = manager.create_source_data(str(source))
    if source.suffix.lower() == ".fbx":
        translator = manager.get_translator_for_source_data(src)
        check(isinstance(translator, ue.InterchangeFbxTranslator), f"Unexpected translator: {translator}")
        # Interchange reads the active translator settings independently of the
        # pipeline stack. Check them before import and again on the saved mesh.
        check_properties(translator.get_settings(), TRANSLATOR_SETTINGS)
    stack = ue.InterchangePipelineStackOverride()
    stack.add_pipeline(pipeline)
    args = ue.ImportAssetParameters()
    args.is_automated = True
    args.replace_existing = False
    args.override_pipelines = stack.override_pipelines
    assets = manager.import_asset(f"{ROOT}/{folder}", src, args)
    check(assets and len(assets) == 1, f"Expected one asset from {source}, got {assets}")
    return assets[0]


def mesh_pipeline():
    pipe = ue.InterchangeGenericAssetsPipeline()
    pipe.import_offset_uniform_scale = 1.0
    mesh = pipe.mesh_pipeline
    mesh.combine_static_meshes_behavior = ue.InterchangeCombineStaticMeshesBehavior.DO_NOT_COMBINE
    mesh.collision = False
    mesh.fallback_collision_type = ue.InterchangeMeshCollision.NONE
    mesh.generate_lightmap_u_vs = True
    mesh.import_skeletal_meshes = False
    mesh.import_geometry_caches = False
    mesh.build_nanite = False  # Low-poly moving props.
    pipe.common_meshes_properties.recompute_normals = False
    pipe.common_meshes_properties.recompute_tangents = True
    pipe.common_meshes_properties.use_mikk_t_space = True
    pipe.material_pipeline.import_materials = False
    pipe.material_pipeline.texture_pipeline.import_textures = False
    return pipe


def texture_settings(name):
    if name.endswith("_BC"):
        return {"srgb": True, "compression_settings": PACK.TC_DEFAULT}
    if name.endswith("_N"):
        return {"srgb": False, "compression_settings": PACK.TC_NORMALMAP, "flip_green_channel": False}
    return {"srgb": False, "compression_settings": PACK.TC_MASKS}


def new_texture(name, expected):
    tex = import_one(SOURCE / "Textures" / f"{name}.png", "Textures", ue.InterchangeGenericAssetsPipeline())
    for key, value in expected.items():
        tex.set_editor_property(key, value)
    return tex


def new_material(name, instance=False):
    cls, factory = ((ue.MaterialInstanceConstant, ue.MaterialInstanceConstantFactoryNew)
                    if instance else (ue.Material, ue.MaterialFactoryNew))
    return ue.AssetToolsHelpers.get_asset_tools().create_asset(name, f"{ROOT}/Materials", cls, factory())


def node(mat, cls, x, y, **properties):
    expr = MAT.create_material_expression(mat, cls, x, y)
    for key, value in properties.items():
        expr.set_editor_property(key, value)
    return expr


def output(expr, prop, pin=""):
    check(MAT.connect_material_property(expr, pin, prop), f"{expr.get_name()} -> {prop}")


def connect(expr, pin, target, inlet):
    check(MAT.connect_material_expressions(expr, pin, target, inlet), f"{expr.get_name()} -> {target}.{inlet}")


def make_master(name, textures, flat):
    mat = new_material(name)
    for i, (param, tex, sampler) in enumerate((
        ("BaseColor", textures["T_PaintDark_BC"], SAMPLE.SAMPLERTYPE_COLOR),
        ("ORM", textures["T_Paint_ORM"], SAMPLE.SAMPLERTYPE_MASKS),
        ("Normal", flat, SAMPLE.SAMPLERTYPE_NORMAL),
    )):
        expr = node(mat, ue.MaterialExpressionTextureSampleParameter2D, -420, 300 * i,
                    parameter_name=param, texture=tex, sampler_type=sampler)
        if param == "ORM":
            output(expr, PROP.MP_AMBIENT_OCCLUSION, "R")
            output(expr, PROP.MP_ROUGHNESS, "G")
            output(expr, PROP.MP_METALLIC, "B")
        else:
            output(expr, PROP.MP_BASE_COLOR if param == "BaseColor" else PROP.MP_NORMAL, "RGB")
    MAT.recompile_material(mat)
    return mat


def verify_master(mat):
    for prop, param, pin in (
        (PROP.MP_BASE_COLOR, "BaseColor", "RGB"), (PROP.MP_AMBIENT_OCCLUSION, "ORM", "R"),
        (PROP.MP_ROUGHNESS, "ORM", "G"), (PROP.MP_METALLIC, "ORM", "B"), (PROP.MP_NORMAL, "Normal", "RGB"),
    ):
        expr = MAT.get_material_property_input_node(mat, prop)
        check(isinstance(expr, ue.MaterialExpressionTextureSampleParameter2D)
              and str(expr.get_editor_property("parameter_name")) == param, f"{mat.get_name()}: {param} -> {prop}")
        actual = MAT.get_material_property_input_node_output_name(mat, prop)
        check(actual == pin or (pin == "RGB" and not actual), f"{mat.get_name()}: expected output {pin}, got {actual}")


def make_instance(name, master, bindings):
    inst = new_material(name, instance=True)
    MAT.set_material_instance_parent(inst, master)
    for param, texture in bindings.items():
        MAT.set_material_instance_texture_parameter_value(inst, param, texture)
    MAT.update_material_instance(inst)
    return inst


def verify_instance(inst, master, bindings):
    check(inst.get_editor_property("parent") == master, f"{inst.get_name()}: parent")
    for key, value in bindings.items():
        check(MAT.get_material_instance_texture_parameter_value(inst, key) == value, f"{inst.get_name()}: {key}")


def srgb_hex(hex_string):
    def linear(v):
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return ue.LinearColor(*[linear(v / 255.0) for v in bytes.fromhex(hex_string)], 1.0)


def solid_material(name, values):
    color, metallic, roughness = values
    mat = new_material(name)
    output(node(mat, ue.MaterialExpressionConstant3Vector, -380, 0, constant=srgb_hex(color)), PROP.MP_BASE_COLOR)
    output(node(mat, ue.MaterialExpressionConstant, -380, 130, r=metallic), PROP.MP_METALLIC)
    output(node(mat, ue.MaterialExpressionConstant, -380, 220, r=roughness), PROP.MP_ROUGHNESS)
    MAT.recompile_material(mat)
    return mat


def verify_solid(mat, values):
    color, metallic, roughness = values
    expr = MAT.get_material_property_input_node(mat, PROP.MP_BASE_COLOR)
    check(expr and expr.constant == srgb_hex(color), f"{mat.get_name()}: BaseColor")
    for prop, val in ((PROP.MP_METALLIC, metallic), (PROP.MP_ROUGHNESS, roughness)):
        expr = MAT.get_material_property_input_node(mat, prop)
        check(expr and math.isclose(expr.get_editor_property("r"), val, abs_tol=1e-6), f"{mat.get_name()}: {prop}")


def indicator_material(name, mask):
    mat = new_material(name)
    output(node(mat, ue.MaterialExpressionConstant3Vector, -320, -180, constant=srgb_hex("070809")), PROP.MP_BASE_COLOR)
    sample = node(mat, ue.MaterialExpressionTextureSample, -930, 0, texture=mask, sampler_type=SAMPLE.SAMPLERTYPE_MASKS)
    color = node(mat, ue.MaterialExpressionVectorParameter, -930, 320,
                 parameter_name="IndicatorColor", default_value=ue.LinearColor(1, 1, 1, 1))
    intensity = node(mat, ue.MaterialExpressionScalarParameter, -640, 360,
                     parameter_name="Intensity", default_value=1.0)
    tint = node(mat, ue.MaterialExpressionMultiply, -640, 120)
    emission = node(mat, ue.MaterialExpressionMultiply, -350, 120)
    connect(sample, "R", tint, "A")
    connect(color, "", tint, "B")
    connect(tint, "", emission, "A")
    connect(intensity, "", emission, "B")
    output(emission, PROP.MP_EMISSIVE_COLOR)
    MAT.recompile_material(mat)
    return mat


def verify_indicator(mat, mask):
    emitter = MAT.get_material_property_input_node(mat, PROP.MP_EMISSIVE_COLOR)
    check(isinstance(emitter, ue.MaterialExpressionMultiply), "M_Indicator: emissive multiply")
    product, intensity = MAT.get_inputs_for_material_expression(mat, emitter)
    sample, color = MAT.get_inputs_for_material_expression(mat, product)
    check(sample.texture == mask and MAT.get_input_node_output_name_for_material_expression(product, sample) == "R",
          "M_Indicator: mask channel")
    check(str(color.get_editor_property("parameter_name")) == "IndicatorColor"
          and str(intensity.get_editor_property("parameter_name")) == "Intensity", "M_Indicator: parameter names")


def new_mesh(name, materials):
    mesh = import_one(SOURCE / "FBX" / f"{name}.fbx", "Meshes", mesh_pipeline())
    for i, slot in enumerate(mesh.static_materials):
        mesh.set_material(i, materials[str(slot.material_slot_name)])
    return mesh


def verify_mesh(mesh, materials, ref):
    name = mesh.get_name()
    slots = {str(s.material_slot_name): s.material_interface for s in mesh.static_materials}
    check(set(slots) == set(ref["materials"]), f"{name}: slot names {slots.keys()}")
    check(all(materials[k] == v for k, v in slots.items()), f"{name}: slot bindings")
    data = mesh.get_editor_property("asset_import_data")
    check(isinstance(data, ue.InterchangeAssetImportData), f"{name}: Interchange metadata")
    # UE 5.8.4 keeps the FBX translator object during import, but may
    # serialize only the asset pipeline when translator defaults were used.
    # The min/max check below independently checks the imported axes and units.
    translator = data.get_translator_settings()
    if translator:
        check_properties(translator, TRANSLATOR_SETTINGS)
    pipe, = data.get_pipelines()
    check_properties(pipe, {"import_offset_uniform_scale": 1.0})
    check_properties(pipe.common_meshes_properties, {
        "recompute_normals": False, "recompute_tangents": True, "use_mikk_t_space": True,
    })
    check_properties(pipe.mesh_pipeline, {
        "generate_lightmap_u_vs": True, "collision": False, "fallback_collision_type": ue.InterchangeMeshCollision.NONE,
        "combine_static_meshes_behavior": ue.InterchangeCombineStaticMeshesBehavior.DO_NOT_COMBINE,
    })
    body = mesh.get_editor_property("body_setup")
    if body:
        agg = body.get_editor_property("agg_geom")
        for shape in ("sphere_elems", "box_elems", "sphyl_elems", "convex_elems", "tapered_capsule_elems"):
            check(len(agg.get_editor_property(shape)) == 0, f"{name}: unexpected {shape}")
    check(mesh.get_num_tex_coords(0) >= 2, f"{name}: missing generated lightmap UVs")
    box = mesh.get_bounding_box()
    for label, vector in (("bbox_min_cm", box.min), ("bbox_max_cm", box.max)):
        actual = (vector.x, vector.y, vector.z)
        check(all(abs(a - b) < 0.02 for a, b in zip(actual, ref[label])), f"{name}: {label} {actual}, expected {ref[label]}")
    ue.log(f"StatekFPP measurements {name}: {box.min} .. {box.max}; {mesh.get_num_triangles(0)} triangles; "
           f"{mesh.get_num_tex_coords(0)} UV channels; slots {', '.join(slots)}")


def run():
    ref = json.loads((SOURCE / "Raport" / "weryfikacja.json").read_text(encoding="utf-8"))["ue"]
    for folder, names, ext in (("Textures", TEXTURES, ".png"), ("FBX", MESHES, ".fbx")):
        for name in names:
            check((SOURCE / folder / (name + ext)).is_file(), f"Missing source {folder}/{name}{ext}")
    textures = {}
    for name in TEXTURES:
        settings = texture_settings(name)
        textures[name] = get_asset("Textures", name, ue.Texture2D, lambda: new_texture(name, settings),
                                   lambda t: check_properties(t, settings))
    flat = ue.load_asset("/Engine/EngineMaterials/DefaultNormal")
    check(isinstance(flat, ue.Texture2D), "Neutral engine normal missing")
    master_name = "M_StatekPBR"
    master = get_asset("Materials", master_name, ue.Material, lambda: make_master(master_name, textures, flat), verify_master)
    materials = {}
    for slot, row in SURFACES.items():
        name = "MI_" + slot
        bindings = dict(zip(("BaseColor", "ORM", "Normal"), [textures[t] if t else flat for t in row]))
        materials[slot] = get_asset("Materials", name, ue.MaterialInstanceConstant,
                                    lambda: make_instance(name, master, bindings),
                                    lambda i: verify_instance(i, master, bindings))
    for slot, values in SOLIDS.items():
        name = "M_" + slot
        materials[slot] = get_asset("Materials", name, ue.Material, lambda: solid_material(name, values),
                                    lambda m: verify_solid(m, values))
    mask = textures["T_Indicator_Mask"]
    materials["Indicator"] = get_asset("Materials", "M_Indicator", ue.Material,
                                       lambda: indicator_material("M_Indicator", mask), lambda m: verify_indicator(m, mask))
    for name in MESHES:
        get_asset("Meshes", name, ue.StaticMesh, lambda: new_mesh(name, materials),
                  lambda m: verify_mesh(m, materials, ref[name]))
    ue.log("StatekFPP verified 9 meshes, 10 textures and 10 materials.")


if __name__ == "__main__":
    run()
