"""Imports SourceArt into /Game/Art and builds the game's materials.

Runs inside the editor's Python commandlet; use Scripts/ImportArt.bat, which first
runs Tools/generate_textures.py and Tools/generate_models.py.

  SourceArt/Textures/*.png  -> /Game/Art/Textures
  SourceArt/Models/*.obj    -> /Game/Art/Meshes (materials by slot name, sockets from sockets.json)
  materials                 -> /Game/Art/Materials
      M_ArenaSurface  world-space triplanar level material: floor texture on top, wall texture on the sides
      M_ArenaProp     weapons, items and players: colour, detail texture, emissive, rim light
      M_ArenaFX       unlit additive effects (tracers, blasts, muzzle flashes)
It also points the mannequin's material slots at the player materials.
Re-running replaces everything, so edit a generator and re-run.
"""

import json
import os

import unreal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURE_DIR = os.path.join(ROOT, "SourceArt", "Textures")
MODEL_DIR = os.path.join(ROOT, "SourceArt", "Models")
TEXTURES = "/Game/Art/Textures"
MATERIALS = "/Game/Art/Materials"
MESHES = "/Game/Art/Meshes"
MANNEQUIN = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(message):
    unreal.log("ArenaImport: " + message)


def import_files(files, destination):
    tasks = []
    for path in files:
        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = destination
        task.destination_name = os.path.splitext(os.path.basename(path))[0]
        task.replace_existing = True
        task.automated = True
        task.save = False
        tasks.append(task)
    tools.import_asset_tasks(tasks)
    for task in tasks:
        paths = list(task.imported_object_paths)
        log("{} -> {}".format(os.path.basename(task.filename), paths[0] if paths else "FAILED"))


# --- textures --------------------------------------------------------------------

def import_textures():
    files = sorted(os.path.join(TEXTURE_DIR, f) for f in os.listdir(TEXTURE_DIR) if f.endswith(".png"))
    import_files(files, TEXTURES)
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        tex = unreal.load_asset(TEXTURES + "/" + name)
        if name.endswith("_N"):
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
        elif name == "T_Grime":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
            tex.set_editor_property("srgb", False)
        EAL.save_loaded_asset(tex)


# --- material helpers --------------------------------------------------------------

def fresh_material(name):
    # Start from an empty asset (the instances made below re-parent to it).
    # MaterialEditingLibrary.delete_all_material_expressions skips nodes, so don't use it.
    path = MATERIALS + "/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
        unreal.SystemLibrary.collect_garbage()
    return tools.create_asset(name, MATERIALS, unreal.Material, unreal.MaterialFactoryNew())


def node(mat, cls, x, y, **props):
    expr = MEL.create_material_expression(mat, cls, x, y)
    for key, value in props.items():
        expr.set_editor_property(key, value)
    return expr


def scalar(mat, name, value, x, y):
    return node(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def vector(mat, name, value, x, y):
    return node(mat, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                default_value=unreal.LinearColor(*value))


def texture_object(mat, name, texture, sampler, x, y):
    return node(mat, unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name,
                texture=unreal.load_asset(TEXTURES + "/" + texture), sampler_type=sampler)


def custom(mat, description, code, output_type, inputs, x, y):
    """A Custom HLSL node; inputs is a list of (name, expression)."""
    expr = node(mat, unreal.MaterialExpressionCustom, x, y, code=code, description=description,
                output_type=output_type)
    pins = []
    for name, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    expr.set_editor_property("inputs", pins)
    for name, source in inputs:
        MEL.connect_material_expressions(source, "", expr, name)
    return expr


def mask(mat, source, r, g, b, a, x, y):
    expr = node(mat, unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=a)
    MEL.connect_material_expressions(source, "", expr, "")
    return expr


def finish(mat):
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("built " + mat.get_name())


COLOR = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
NORMAL = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
GRAYSCALE = unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE
FLOAT3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
FLOAT4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4
FLOAT1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1

# Shared by both triplanar nodes: blend weights and the three planar projections.
# Side faces map world "up" to the texture's up, tops map +X right and +Y down.
TRIPLANAR_SETUP = """
float3 n = normalize(N);
float3 w = pow(abs(n), 4.0);
w /= (w.x + w.y + w.z);
float2 uvX = float2(WP.y, -WP.z);
float2 uvY = float2(WP.x, -WP.z);
float2 uvZ = float2(WP.x, WP.y);
"""

SURFACE_COLOR = TRIPLANAR_SETUP + """
float4 cx = Texture2DSample(SideTex, SideTexSampler, uvX * SideScale);
float4 cy = Texture2DSample(SideTex, SideTexSampler, uvY * SideScale);
float4 cz = n.z > 0.0 ? Texture2DSample(TopTex, TopTexSampler, uvZ * TopScale)
                      : Texture2DSample(SideTex, SideTexSampler, uvZ * SideScale);
float4 c = cx * w.x + cy * w.y + cz * w.z;
float g = Texture2DSample(Grime, GrimeSampler, uvX * GrimeScale).r * w.x
        + Texture2DSample(Grime, GrimeSampler, uvY * GrimeScale).r * w.y
        + Texture2DSample(Grime, GrimeSampler, uvZ * GrimeScale).r * w.z;
c.rgb *= Tint * 2.0 * lerp(1.0, 0.55 + 0.9 * g, GrimeStrength);
c.a = saturate(c.a + (0.5 - g) * 0.3 * GrimeStrength);
return c;
"""

# Tangent-space normals are rebuilt into world space per projection
# (T_u, T_v, N): X=(+Y,-Z,±X), Y=(+X,-Z,±Y), Z=(+X,+Y,±Z).
SURFACE_NORMAL = TRIPLANAR_SETUP + """
float2 tx = (Texture2DSample(SideNrm, SideNrmSampler, uvX * SideScale).rg * 2.0 - 1.0) * Strength;
float2 ty = (Texture2DSample(SideNrm, SideNrmSampler, uvY * SideScale).rg * 2.0 - 1.0) * Strength;
float2 tz = n.z > 0.0 ? Texture2DSample(TopNrm, TopNrmSampler, uvZ * TopScale).rg
                      : Texture2DSample(SideNrm, SideNrmSampler, uvZ * SideScale).rg;
tz = (tz * 2.0 - 1.0) * Strength;
float3 nx = float3(sign(n.x) * sqrt(saturate(1.0 - dot(tx, tx))), tx.x, -tx.y);
float3 ny = float3(ty.x, sign(n.y) * sqrt(saturate(1.0 - dot(ty, ty))), -ty.y);
float3 nz = float3(tz.x, tz.y, sign(n.z) * sqrt(saturate(1.0 - dot(tz, tz))));
return normalize(nx * w.x + ny * w.y + nz * w.z);
"""


def build_surface_material():
    mat = fresh_material("M_ArenaSurface")
    mat.set_editor_property("tangent_space_normal", False)
    wp = node(mat, unreal.MaterialExpressionWorldPosition, -1400, 0)
    nrm = node(mat, unreal.MaterialExpressionVertexNormalWS, -1400, 100)
    top = texture_object(mat, "TopAlbedo", "T_Tiles_D", COLOR, -1400, 200)
    side = texture_object(mat, "SideAlbedo", "T_Panels_D", COLOR, -1400, 400)
    top_n = texture_object(mat, "TopNormal", "T_Tiles_N", NORMAL, -1400, 600)
    side_n = texture_object(mat, "SideNormal", "T_Panels_N", NORMAL, -1400, 800)
    grime = texture_object(mat, "Grime", "T_Grime", GRAYSCALE, -1400, 1000)
    top_scale = scalar(mat, "TopScale", 1.0 / 256.0, -1100, 0)
    side_scale = scalar(mat, "SideScale", 1.0 / 400.0, -1100, 100)
    grime_scale = scalar(mat, "GrimeScale", 1.0 / 2500.0, -1100, 200)
    grime_strength = scalar(mat, "GrimeStrength", 0.6, -1100, 300)
    strength = scalar(mat, "NormalStrength", 1.0, -1100, 400)
    tint = vector(mat, "Tint", (0.5, 0.5, 0.5, 1.0), -1100, 500)

    color = custom(mat, "TriplanarColor", SURFACE_COLOR, FLOAT4, [
        ("WP", wp), ("N", nrm), ("TopTex", top), ("SideTex", side), ("Grime", grime),
        ("TopScale", top_scale), ("SideScale", side_scale), ("GrimeScale", grime_scale),
        ("GrimeStrength", grime_strength), ("Tint", tint)], -700, 0)
    normal = custom(mat, "TriplanarNormal", SURFACE_NORMAL, FLOAT3, [
        ("WP", wp), ("N", nrm), ("TopNrm", top_n), ("SideNrm", side_n),
        ("TopScale", top_scale), ("SideScale", side_scale), ("Strength", strength)], -700, 400)
    MEL.connect_material_property(mask(mat, color, True, True, True, False, -400, 0), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(mask(mat, color, False, False, False, True, -400, 150), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    finish(mat)


PROP_COLOR = """
float4 d = Texture2DSample(Detail, DetailSampler, UV * DetailScale);
float3 c = Color * lerp(1.0, d.rgb * 1.15, DetailStrength);
return float4(c, saturate(lerp(Roughness, Roughness * (0.4 + d.a * 1.2), DetailStrength)));
"""

PROP_NORMAL = """
float2 t = (Texture2DSample(DetailN, DetailNSampler, UV * DetailScale).rg * 2.0 - 1.0) * DetailStrength;
return float3(t, sqrt(saturate(1.0 - dot(t, t))));
"""

PROP_EMISSIVE = """
float rim = pow(1.0 - saturate(dot(normalize(N), normalize(V))), 3.0);
float pulse = 1.0 + Pulse * sin(Time * 5.0);
return Color * Emissive * pulse + RimColor * rim * RimStrength;
"""


def build_prop_material():
    mat = fresh_material("M_ArenaProp")
    mat.set_editor_property("tangent_space_normal", True)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
    detail = texture_object(mat, "Detail", "T_Metal_D", COLOR, -1400, 100)
    detail_n = texture_object(mat, "DetailNormal", "T_Metal_N", NORMAL, -1400, 300)
    color = vector(mat, "Color", (0.5, 0.5, 0.5, 1.0), -1100, 0)
    rim_color = vector(mat, "RimColor", (1.0, 1.0, 1.0, 1.0), -1100, 150)
    metallic = scalar(mat, "Metallic", 0.0, -1100, 300)
    roughness = scalar(mat, "Roughness", 0.5, -1100, 400)
    emissive = scalar(mat, "Emissive", 0.0, -1100, 500)
    rim = scalar(mat, "RimStrength", 0.0, -1100, 600)
    pulse = scalar(mat, "Pulse", 0.0, -1100, 700)
    detail_strength = scalar(mat, "DetailStrength", 0.5, -1100, 800)
    detail_scale = scalar(mat, "DetailScale", 1.0, -1100, 900)
    normal_ws = node(mat, unreal.MaterialExpressionVertexNormalWS, -1100, 1000)
    camera = node(mat, unreal.MaterialExpressionCameraVectorWS, -1100, 1100)
    time = node(mat, unreal.MaterialExpressionTime, -1100, 1200)

    shade = custom(mat, "PropColor", PROP_COLOR, FLOAT4, [
        ("UV", uv), ("Detail", detail), ("Color", color), ("Roughness", roughness),
        ("DetailStrength", detail_strength), ("DetailScale", detail_scale)], -700, 0)
    normal = custom(mat, "PropNormal", PROP_NORMAL, FLOAT3, [
        ("UV", uv), ("DetailN", detail_n), ("DetailStrength", detail_strength), ("DetailScale", detail_scale)], -700, 300)
    glow = custom(mat, "PropEmissive", PROP_EMISSIVE, FLOAT3, [
        ("Color", color), ("Emissive", emissive), ("RimColor", rim_color), ("RimStrength", rim),
        ("Pulse", pulse), ("Time", time), ("N", normal_ws), ("V", camera)], -700, 600)
    MEL.connect_material_property(mask(mat, shade, True, True, True, False, -400, 0), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(mask(mat, shade, False, False, False, True, -400, 150), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mat.set_editor_property("used_with_skeletal_mesh", True)
    finish(mat)


FX_EMISSIVE = """
float f = 1.0;
if (FresnelExp > 0.0)
{
    f = pow(1.0 - saturate(abs(dot(normalize(N), normalize(V)))), FresnelExp) * 1.5 + 0.1;
}
return Color * Intensity * Opacity * f;
"""


def build_fx_material():
    mat = fresh_material("M_ArenaFX")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    color = vector(mat, "Color", (1.0, 0.6, 0.2, 1.0), -900, 0)
    intensity = scalar(mat, "Intensity", 6.0, -900, 150)
    opacity = scalar(mat, "Opacity", 1.0, -900, 250)
    fresnel = scalar(mat, "FresnelExp", 0.0, -900, 350)
    normal_ws = node(mat, unreal.MaterialExpressionVertexNormalWS, -900, 450)
    camera = node(mat, unreal.MaterialExpressionCameraVectorWS, -900, 550)
    glow = custom(mat, "FXEmissive", FX_EMISSIVE, FLOAT3, [
        ("Color", color), ("Intensity", intensity), ("Opacity", opacity), ("FresnelExp", fresnel),
        ("N", normal_ws), ("V", camera)], -500, 0)
    MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat)


def make_instance(name, parent, scalars=None, vectors=None):
    path = MATERIALS + "/" + name
    if EAL.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = tools.create_asset(name, MATERIALS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, unreal.load_asset(MATERIALS + "/" + parent))
    MEL.clear_all_material_instance_parameters(mi)
    for key, value in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, key, value)
    for key, value in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, key, unreal.LinearColor(*value))
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi


def build_instances():
    make_instance("MI_Metal", "M_ArenaProp",
                  {"Metallic": 1.0, "Roughness": 0.32, "DetailStrength": 0.6},
                  {"Color": (0.42, 0.43, 0.46, 1.0)})
    make_instance("MI_Dark", "M_ArenaProp",
                  {"Metallic": 0.0, "Roughness": 0.65, "DetailStrength": 0.35},
                  {"Color": (0.03, 0.03, 0.035, 1.0)})
    make_instance("MI_Accent", "M_ArenaProp",
                  {"Metallic": 0.3, "Roughness": 0.3, "DetailStrength": 0.35},
                  {"Color": (0.8, 0.3, 0.1, 1.0)})
    make_instance("MI_Glow", "M_ArenaProp",
                  {"Emissive": 12.0, "Roughness": 0.2, "DetailStrength": 0.0},
                  {"Color": (0.3, 0.6, 1.0, 1.0)})
    make_instance("MI_PlayerArmor", "M_ArenaProp",
                  {"Metallic": 0.35, "Roughness": 0.3, "DetailStrength": 0.25, "RimStrength": 0.7, "DetailScale": 2.0},
                  {"Color": (0.5, 0.5, 0.5, 1.0)})
    make_instance("MI_PlayerSuit", "M_ArenaProp",
                  {"Metallic": 0.0, "Roughness": 0.55, "DetailStrength": 0.3, "RimStrength": 0.5, "DetailScale": 3.0},
                  {"Color": (0.035, 0.035, 0.04, 1.0)})
    make_instance("MI_FX", "M_ArenaFX")
    log("built material instances")


# --- meshes --------------------------------------------------------------------------

SLOT_MATERIALS = {
    "Metal": "MI_Metal",
    "Dark": "MI_Dark",
    "Accent": "MI_Accent",
    "Glow": "MI_Glow",
    "FX": "MI_FX",
}


def import_meshes():
    files = sorted(os.path.join(MODEL_DIR, f) for f in os.listdir(MODEL_DIR) if f.endswith(".obj"))
    import_files(files, MESHES)
    with open(os.path.join(MODEL_DIR, "sockets.json")) as f:
        sockets = json.load(f)
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        mesh = unreal.load_asset(MESHES + "/" + name)
        if not mesh:
            continue
        materials = list(mesh.get_editor_property("static_materials"))
        for slot in materials:
            mi = SLOT_MATERIALS.get(str(slot.material_slot_name))
            if mi:
                slot.set_editor_property("material_interface", unreal.load_asset(MATERIALS + "/" + mi))
        mesh.set_editor_property("static_materials", materials)

        for socket_name, location in sockets.get(name, {}).items():
            old = mesh.find_socket(socket_name)
            if old:
                mesh.remove_socket(old)
            socket = unreal.StaticMeshSocket(mesh)
            socket.set_editor_property("socket_name", socket_name)
            # Sockets are authored in Unreal's frame, same as the model.
            socket.set_editor_property("relative_location", unreal.Vector(*location))
            mesh.add_socket(socket)
        EAL.save_loaded_asset(mesh)
    # Interchange may add placeholder materials next to the meshes; they are unused.
    for asset in EAL.list_assets(MESHES, recursive=True):
        asset_name = asset.split("/")[-1].split(".")[0]
        if not asset_name.startswith("SM_"):
            EAL.delete_asset(asset)


def skin_mannequin():
    mesh = unreal.load_asset(MANNEQUIN)
    slots = list(mesh.get_editor_property("materials"))
    for slot in slots:
        name = str(slot.get_editor_property("material_slot_name"))
        mi = "MI_PlayerArmor" if name == "M_Torso" else "MI_PlayerSuit"
        slot.set_editor_property("material_interface", unreal.load_asset(MATERIALS + "/" + mi))
    mesh.set_editor_property("materials", slots)
    EAL.save_loaded_asset(mesh)
    log("skinned " + mesh.get_name())


import_textures()
build_surface_material()
build_prop_material()
build_fx_material()
build_instances()
import_meshes()
skin_mannequin()
log("done")
